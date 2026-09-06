// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using System.Threading.Tasks;
using System.Runtime.InteropServices;

internal sealed partial class PreviewLauncher : PreviewForm {
    readonly JavaScriptSerializer json = new JavaScriptSerializer { MaxJsonLength = 4000000 };
    Dictionary<string,object> settings;
    string root, original;
    Process game;
    FileStream guard;
    byte[] originalBytes;
    DateTime exitTime;
    bool settling;
    readonly Label status = new Label();
    readonly Button launch = new Button(), capture = new Button(), restore = new Button();
    readonly Timer timer = new Timer();
    readonly Panel moreOptions=new Panel();
    string lastCapture = "";
    HashSet<string> capturesBeforeRequest = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    bool capturePending;
    DateTime launchedAt;
    bool originalChanged;

    static string Hash(byte[] b) { using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(b)).Replace("-","").ToLowerInvariant(); }
    static string HashFile(string p) { using(var s=File.OpenRead(p))using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(s)).Replace("-","").ToLowerInvariant(); }
    string S(string key) { return (string)settings[key]; }
    string Inside(string p) {
        var result=Path.GetFullPath(p);
        PreviewFiles.NoReparse(result);
        if(!result.StartsWith(Path.GetFullPath(root).TrimEnd('\\')+"\\",StringComparison.OrdinalIgnoreCase))throw new Exception("A preview file points outside its prepared folder.");
        return result;
    }
    void Log(string text) { try{File.AppendAllText(Path.Combine(root,"launcher.log"),DateTime.UtcNow.ToString("o")+" "+text+Environment.NewLine);}catch(IOException){}catch(UnauthorizedAccessException){} }
    void OpenFolderOrGuide(string path){try{Process.Start(new ProcessStartInfo(path){UseShellExecute=true});}catch(Exception e){status.Text="Could not open this item. "+e.Message;}}
    void LoadSettings() {
        settings=json.Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"preview.json")));
        root=Path.GetFullPath(S("renderer_root"));original=S("original_config");
        if(!String.Equals(root.TrimEnd('\\'),AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase))
            throw new IOException("This preview folder moved after setup. Prepare a new installation in its final location.");
        if(File.Exists(Path.Combine(root,"setup-incomplete.txt")))throw new IOException("Setup did not finish. Open the release installer to prepare a new preview.");
        Inside(S("game_exe"));Inside(S("game_home"));Inside(S("layer_dir"));
        if(!Directory.Exists(root))throw new Exception("The prepared preview folder is missing.");
    }
    void Verify() {
        Requirements.Require();
        foreach(var item in (Dictionary<string,object>)settings["checked_files"]){
            var file=Inside(Path.Combine(root,item.Key));
            if(!File.Exists(file)||HashFile(file)!=(string)item.Value)throw new Exception("A prepared preview component changed: "+Path.GetFileName(file)+". Prepare a fresh preview from the release download.");
        }
        if(HashFile(S("source_exe"))!=S("source_exe_hash"))throw new Exception("Steam has updated ETS2 since this preview was prepared. Prepare a new preview for the supported game version.");
        foreach(var entry in (Dictionary<string,object>)settings["source_archives"]) {
            var file=new FileInfo(entry.Key);var expected=(Dictionary<string,object>)entry.Value;
            if(!file.Exists||file.Length!=Convert.ToInt64(expected["length"])||file.LastWriteTimeUtc.Ticks!=Convert.ToInt64(expected["modified"]))throw new Exception("Steam has changed a game archive. Prepare a new preview before continuing.");
        }
        var runtime=Microsoft.Win32.Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1","ActiveRuntime",null) as string;
        if(String.IsNullOrEmpty(runtime)||!File.Exists(runtime))throw new Exception("Windows has no available OpenXR runtime. Connect Virtual Desktop before launching.");
        if(runtime.IndexOf("fixture",StringComparison.OrdinalIgnoreCase)>=0||runtime.IndexOf("offline",StringComparison.OrdinalIgnoreCase)>=0)throw new Exception("An offline test runtime is selected in Windows. The headset runtime is required.");
    }
    ProcessStartInfo StartInfo() {
        var info=new ProcessStartInfo(S("game_exe"),"-rdevice dx11 -openxr -nointro -noworkshop -pure -homedir \""+S("game_home")+"\"");
        info.WorkingDirectory=Path.GetDirectoryName(S("game_exe"));info.UseShellExecute=false;
        foreach(string key in new [] {"XR_RUNTIME_JSON","ETS2_FEED_OBSERVE_ONLY","ETS2_FEED_CAPTURE_PLANE_MASK","ETS2_FEED_CAPTURE_AFTER","ETS2_XR_IMAGE_AFTER","ETS2_XR_IMAGE_DIR"})info.EnvironmentVariables.Remove(key);
        info.EnvironmentVariables["SteamAppId"]="227300";info.EnvironmentVariables["SteamGameId"]="227300";
        info.EnvironmentVariables["RESHADE_BASE_PATH_OVERRIDE"]=root;
        info.EnvironmentVariables["XR_API_LAYER_PATH"]=S("layer_dir");
        info.EnvironmentVariables["XR_ENABLE_API_LAYERS"]="XR_APILAYER_reshade";
        info.EnvironmentVariables["DISABLE_XR_APILAYER_reshade_1"]="1";
        return info;
    }
    bool Active() { return game!=null&&!game.HasExited; }
    async void LaunchGame(object sender,EventArgs args) {
        if(starting||Active()||settling)return;
        starting=true;launch.Enabled=false;SetOptionsEnabled(false);
        try {
            status.Text="Checking the preview files…";
            await Task.Run(()=>Verify());
            if(Process.GetProcessesByName("steam").Length==0)throw new IOException("Open Steam and sign in before starting the preview.");
            if(Process.GetProcessesByName("eurotrucks2").Length!=0)throw new Exception("Close the running ETS2 session before starting this preview.");
            guard=new FileStream(original,FileMode.Open,FileAccess.Read,FileShare.Read);
            using(var copy=new MemoryStream()){guard.CopyTo(copy);originalBytes=copy.ToArray();}
            File.WriteAllBytes(Path.Combine(root,"original-config-launcher-backup.cfg"),originalBytes);
            File.WriteAllBytes(Path.Combine(root,"regular-settings-before-"+DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff")+".cfg"),originalBytes);
            launchedAt=DateTime.UtcNow;lastCapture="";capturePending=false;originalChanged=false;restore.Enabled=false;
            var selectedQuality=ReadQuality();activePasses=selectedQuality[0];activeQuality=DescribeQuality(selectedQuality);
            if(activePasses<1||activePasses>2||selectedQuality[1]<50||selectedQuality[1]>100||
                (selectedQuality[2]!=0&&(selectedQuality[2]<40||selectedQuality[2]>100)))throw new IOException("Choose a valid quality preset before launching.");
            game=Process.Start(StartInfo());
            launch.Enabled=false;status.Text="Starting VR…\nLoad a local preview profile with Steam Cloud off.";
            Log("Started private headset session pid="+game.Id);timer.Start();
        } catch(Exception e){ if(guard!=null){guard.Dispose();guard=null;}status.Text="Preview did not start. "+e.Message;MessageBox.Show(this,e.Message,"Preview could not start",MessageBoxButtons.OK,MessageBoxIcon.Information); }
        finally{starting=false;if(!Active()){launch.Enabled=true;SetOptionsEnabled(true);}try{RefreshQuality();}catch(Exception e){status.Text="Could not read quality settings. "+e.Message;if(!Active())launch.Enabled=false;}}
    }
    bool HasNeuralFrame() {
        try {
            var path=Path.Combine(root,"native-input-observer.json");
            if(!File.Exists(path)||File.GetLastWriteTimeUtc(path)<launchedAt||(DateTime.UtcNow-File.GetLastWriteTimeUtc(path)).TotalSeconds>15)return false;
            var data=json.Deserialize<Dictionary<string,object>>(File.ReadAllText(path));
            var slots=(IEnumerable)data["actual_nr_parameters"];
            var passes=activePasses;
            if(passes<1||passes>2)return false;
            var frames=new Dictionary<long,HashSet<string>>();
            foreach(Dictionary<string,object> p in slots)if(p!=null&&Convert.ToInt64(p["frame_id"])>0&&Convert.ToInt32(p["result"])==1) {
                var id=Convert.ToInt64(p["frame_id"]);if(!frames.ContainsKey(id))frames[id]=new HashSet<string>();
                frames[id].Add(p["eye"]+"/"+p["pass"]);
            }
            return frames.Values.Any(p=>p.Contains("0/1")&&p.Contains("1/1")&&(passes==1||(p.Contains("0/2")&&p.Contains("1/2"))));
        }catch{return false;}
    }
    bool CaptureComplete(string folder,Dictionary<string,object> session) {
        try {
            if(!session.ContainsKey("complete")||!Convert.ToBoolean(session["complete"])||Convert.ToInt32(session["written"])!=4||Convert.ToInt32(session["requested"])!=4)return false;
            var previous=0L;
            for(var i=0;i<4;i++) {
                var frame=json.Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(folder,"frame-"+i+".json")));
                if(!Convert.ToBoolean(frame["complete"])||!Convert.ToBoolean(frame["comparison"])||Convert.ToInt32(frame["index"])!=i||(string)frame["layout"]!="side_by_side")return false;
                var id=Convert.ToInt64(frame["frame_id"]);if(id<=previous)return false;previous=id;
                var kinds=new HashSet<string>();
                foreach(Dictionary<string,object> image in (IEnumerable)frame["images"]) {
                    var name=(string)image["file"];
                    if(Path.GetFileName(name)!=name||!name.StartsWith("frame-"+i+"-",StringComparison.Ordinal))return false;
                    var width=Convert.ToInt64(image["width"]);var height=Convert.ToInt64(image["height"]);var bpp=Convert.ToInt64(image["bytes_per_pixel"]);
                    if(width<=0||width%2!=0||height<=0||bpp<1||bpp>16)return false;
                    var bytes=checked(width*height*bpp);
                    var file=new FileInfo(Path.Combine(folder,name));
                    if(bytes!=Convert.ToInt64(image["bytes"])||!file.Exists||file.Length!=bytes)return false;
                    if(!kinds.Add((string)image["kind"]))return false;
                }
                foreach(var kind in new[]{"original","depth","motion","mask","result","work-color","work-depth","work-motion","work-result","work-mask"})if(!kinds.Contains(kind))return false;
            }
            return true;
        }catch{return false;}
    }
    void RequestCapture(object sender,EventArgs args) {
        try {
        if(!Active()||!HasNeuralFrame()||capturePending)return;
        if(new DriveInfo(Path.GetPathRoot(root)).AvailableFreeSpace<5L*1024*1024*1024){status.Text="Keep at least 5 GB free before recording comparison frames.";return;}
        var folder=Path.Combine(root,"DLSS5-Captures");
        capturesBeforeRequest=new HashSet<string>(Directory.Exists(folder)?Directory.GetDirectories(folder):new string[0],StringComparer.OrdinalIgnoreCase);
        PreviewFiles.AtomicText(Path.Combine(root,"dlss5-capture.request"),Guid.NewGuid().ToString());
        status.Text="Comparison capture requested.\nKeep the driving scene rendering for a few seconds.";
        capturePending=true;capture.Enabled=false;Log("User requested comparison frames");
        }catch(Exception e){status.Text="Could not request a capture. "+e.Message;}
    }
    void CheckOriginal() {
        var now=File.ReadAllBytes(original);if(Hash(now)==Hash(originalBytes))return;
        originalChanged=true;
    }
    void RestoreOriginal(object sender,EventArgs args) {
        if(Active()||settling||originalBytes==null)return;
        if(MessageBox.Show(this,"Restore the regular graphics settings saved before this preview session? The current settings will also be backed up.","Restore regular settings",MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
        try {
            var now=File.ReadAllBytes(original);
            File.WriteAllBytes(Path.Combine(root,"settings-before-restore-"+DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff")+".cfg"),now);
            File.WriteAllBytes(original,originalBytes);restore.Enabled=false;originalChanged=false;
            status.Text="Your regular graphics settings were restored.\nThe replaced settings are backed up in this preview folder.";
        } catch(Exception e){MessageBox.Show(this,e.Message,"Could not restore settings");}
    }
    void Tick(object sender,EventArgs args) {
        try {
            if(Active()) {
                var ready=HasNeuralFrame();capture.Enabled=ready&&!capturePending;
                RefreshQuality();RefreshComparisonStatus();
                if(capturePending) {
                    var folder=Path.Combine(root,"DLSS5-Captures");
                    var newest=Directory.Exists(folder)?Directory.GetDirectories(folder).Where(p=>!capturesBeforeRequest.Contains(p)).OrderByDescending(p=>p).FirstOrDefault():null;
                    if(newest!=null&&File.Exists(Path.Combine(newest,"session.json"))) {
                        var c=json.Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(newest,"session.json")));
                        if(CaptureComplete(newest,c)){status.Text="Capture saved: four stereo comparisons.\nHome: ReShade settings   ·   End: Snowymoon settings";lastCapture=newest;capturePending=false;}
                        else if(c.ContainsKey("complete")&&Convert.ToBoolean(c["complete"]))status.Text="Capture files are incomplete. Keep the game running.\nIf this persists, leave the folder for diagnosis.";
                        else if(c.ContainsKey("reason"))status.Text="Capture: "+c["reason"];
                    }
                } else if(ready)status.Text=(lastCapture.Length>0?"Comparison capture saved. ":"")+"Neural rendering is active in both eyes.";
                if(!ready&&!capturePending)status.Text="Waiting for the driving scene. Enter the truck to begin.";
                return;
            }
            capture.Enabled=false;RefreshComparisonStatus();
            if(!settling){settling=true;exitTime=DateTime.UtcNow;status.Text="Game closed. Checking your regular settings…";Log("Private game exited code="+(game==null?"unknown":game.ExitCode.ToString()));}
            var seconds=(DateTime.UtcNow-exitTime).TotalSeconds;
            if(seconds>=5&&guard!=null){guard.Dispose();guard=null;}
            if(guard==null)CheckOriginal();
            if(seconds>=20){timer.Stop();settling=false;game=null;launch.Enabled=true;SetOptionsEnabled(true);RefreshQuality();RefreshAppearance();RefreshComparisonStatus();restore.Enabled=originalChanged;status.Text=originalChanged?"Your regular settings changed. Use More options → Restore settings to recover the backup.":"Session finished. Ready for another drive.";}
        }catch(IOException){
            if(!Active()&&settling&&(DateTime.UtcNow-exitTime).TotalSeconds>=20){timer.Stop();settling=false;game=null;launch.Enabled=true;SetOptionsEnabled(true);if(guard!=null){guard.Dispose();guard=null;}restore.Enabled=originalBytes!=null;status.Text="The game closed, but regular settings could not be checked.\nThe settings saved before launch remain backed up in this folder.";}
        }
        catch(Exception e){status.Text=e.Message;Log("Check failed: "+e.Message);if(!Active()){timer.Stop();if(guard!=null){guard.Dispose();guard=null;}settling=false;}}
    }
    [DllImport("kernel32.dll")] static extern ulong GetTickCount64();
    Dictionary<string,object> ReadPreviewStatus() {
        if(!Active())return null;
        var path=Path.Combine(root,"preview-status.json");
        if(!File.Exists(path)||new FileInfo(path).Length>8192||File.GetLastWriteTimeUtc(path)<launchedAt)return null;
        var data=json.Deserialize<Dictionary<string,object>>(File.ReadAllText(path));
        if(Convert.ToInt32(data["schema"])!=1||Convert.ToInt32(data["pid"])!=game.Id||
            (string)data["session"]!=game.StartTime.ToUniversalTime().ToFileTimeUtc().ToString())return null;
        ulong tick=Convert.ToUInt64(data["tick"]),now=GetTickCount64();
        return now>=tick&&now-tick<2000?data:null;
    }
    void RefreshComparisonStatus() {
        try {
            if(!Active()) {
                var key=toggleKey.SelectedIndex>=0&&toggleKey.SelectedIndex<2?toggleKey.Text+": compare  ·  ":"";
                comparisonStatus.Text=key+"Home: ReShade  ·  End: Snowymoon";return;
            }
            var data=ReadPreviewStatus();
            if(data==null){comparisonStatus.Text="Waiting for current VR image status…";return;}
            if(data.ContainsKey("reason")&&(string)data["reason"]=="restart_required"){
                comparisonStatus.Text="Neural rendering stopped. Close ETS2 completely, then launch it again.";return;
            }
            var blend=Convert.ToDouble(data["selected_blend"])*100;
            ulong delivered=Convert.ToUInt64(data["delivered_tick"]),now=GetTickCount64();
            bool applied=Convert.ToBoolean(data["delivery_this_present"])&&Convert.ToUInt64(data["delivered_frame"])>0&&now>=delivered&&now-delivered<2000&&
                Convert.ToUInt64(data["selected_serial"])==Convert.ToUInt64(data["delivered_serial"]);
            comparisonStatus.Text=applied?"Effect: "+blend.ToString("0")+"%"+(blend==0?" — hidden; processing continues":" — visible"):
                "Selected blend: "+blend.ToString("0")+"% — waiting for a matching VR frame";
        }catch{comparisonStatus.Text="VR status is updating…";}
    }
    void SaveDiagnostics(object sender,EventArgs args) {
        try {
            Dictionary<string,object> runtime=null;try{runtime=ReadPreviewStatus();}catch{}
            var report=new {schema=1,preview="0.1",created_utc=DateTime.UtcNow.ToString("o"),game_running=Active(),quality=ReadQuality(),active_quality=activeQuality,
                recent_neural_evaluations=Active()&&HasNeuralFrame(),vr_image_status=runtime,component_hashes=settings["checked_files"]};
            var path=Path.Combine(root,"preview-diagnostics.json");PreviewFiles.AtomicText(path,json.Serialize(report));
            status.Text="Diagnostics saved in preview-diagnostics.json.\nIt contains component versions and status; no saves, account files or images.";
        }catch(Exception e){status.Text="Could not save diagnostics. "+e.Message;}
    }
    static void CreateHandles(Control parent){var handle=parent.Handle;foreach(Control child in parent.Controls)CreateHandles(child);parent.PerformLayout();}
    static Button QuietButton(string text,int x,int y,int width) {
        var button=new Button{Text=text,Location=new Point(x,y),Size=new Size(width,36),FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(245,247,246)};
        button.FlatAppearance.BorderSize=0;return button;
    }
    PreviewLauncher() {
        LoadSettings();Text="ETS2 DLSS 5 VR Preview";ClientSize=new Size(640,554);Font=new Font("Segoe UI",10);BackColor=Color.FromArgb(245,247,246);ForeColor=Color.FromArgb(28,39,49);StartPosition=FormStartPosition.CenterScreen;MaximizeBox=false;FormBorderStyle=FormBorderStyle.FixedDialog;
        Controls.Add(new Label{Text="ETS2 DLSS 5 VR",Font=new Font("Segoe UI",24,FontStyle.Bold),Location=new Point(24,24),Size=new Size(458,45)});
        Controls.Add(new Label{Text="PREVIEW 0.1",Font=new Font("Segoe UI",9,FontStyle.Bold),ForeColor=Color.FromArgb(39,105,89),Location=new Point(495,40),Size=new Size(117,23),TextAlign=ContentAlignment.MiddleRight});
        moreOptions.Location=new Point(28,560);moreOptions.Size=new Size(584,90);moreOptions.Visible=false;Controls.Add(moreOptions);
        BuildOptions();
        launch.Text="Start VR";launch.Location=new Point(28,344);launch.Size=new Size(350,46);launch.BackColor=Color.FromArgb(39,105,89);launch.ForeColor=Color.White;launch.FlatStyle=FlatStyle.Flat;launch.FlatAppearance.BorderSize=0;launch.Click+=LaunchGame;
        capture.Text="Capture comparison";capture.Location=new Point(394,344);capture.Size=new Size(218,46);capture.FlatStyle=FlatStyle.Flat;capture.FlatAppearance.BorderColor=Color.FromArgb(192,204,198);capture.Enabled=false;capture.Click+=RequestCapture;
        status.Text="Connect your headset and open Steam, then start VR.";status.Location=new Point(28,412);status.Size=new Size(584,48);status.Font=new Font("Segoe UI",10);
        comparisonStatus.Location=new Point(28,469);comparisonStatus.Size=new Size(584,30);comparisonStatus.Font=new Font("Segoe UI",9);comparisonStatus.ForeColor=Color.FromArgb(82,97,109);RefreshComparisonStatus();
        var files=QuietButton("Open captures",20,510,136);files.Click+=(s,e)=>OpenFolderOrGuide(Path.Combine(root,"DLSS5-Captures"));
        var guide=QuietButton("Setup guide ↗",166,510,136);guide.Click+=(s,e)=>OpenFolderOrGuide(Path.Combine(root,"Read me first.html"));
        var more=QuietButton("More options +",454,510,166);more.Click+=(s,e)=>{AutoScrollPosition=Point.Empty;moreOptions.Visible=!moreOptions.Visible;more.Text=moreOptions.Visible?"Fewer options −":"More options +";int height=moreOptions.Visible?674:554;AutoScroll=true;AutoScrollMinSize=new Size(0,height);ClientSize=new Size(640,RenderOnly?height:Math.Min(height,Math.Max(440,Screen.FromControl(this).WorkingArea.Height-80)));};
        var diagnostic=QuietButton("Save diagnostics",206,25,164);diagnostic.Click+=SaveDiagnostics;moreOptions.Controls.Add(diagnostic);
        restore.Text="Restore settings";restore.Location=new Point(386,25);restore.Size=new Size(198,36);restore.Enabled=false;restore.FlatStyle=FlatStyle.Flat;restore.FlatAppearance.BorderColor=Color.FromArgb(192,204,198);restore.Click+=RestoreOriginal;moreOptions.Controls.Add(restore);
        Controls.AddRange(new Control[]{launch,capture,status,comparisonStatus,files,guide,more});timer.Interval=1000;timer.Tick+=Tick;
        var tips=new ToolTip();tips.SetToolTip(capture,"Records four stereo frames. This can briefly pause rendering and uses several GB of disk space.");tips.SetToolTip(restore,"Recover your regular game's graphics settings from the backup saved before launch.");
        FormClosing+=(s,e)=>{if(starting||Active()||settling){e.Cancel=true;MessageBox.Show(this,"Close ETS2 first and let the settings check finish.","Preview session is active",MessageBoxButtons.OK,MessageBoxIcon.Information);}};
    }
    [STAThread] static int Main(string[] args) {
        try {
            // Some parent processes supply duplicate Windows environment names with different casing.
            // Normalize only this process; Framework's child-environment dictionary otherwise throws.
            var inherited=Environment.GetEnvironmentVariables().Cast<DictionaryEntry>().GroupBy(e=>(string)e.Key,StringComparer.OrdinalIgnoreCase);
            foreach(var group in inherited.Where(g=>g.Count()>1).ToArray()) {
                var entries=group.ToArray();var chosen=entries.First();
                foreach(var entry in entries)Environment.SetEnvironmentVariable((string)entry.Key,null,EnvironmentVariableTarget.Process);
                Environment.SetEnvironmentVariable((string)chosen.Key,(string)chosen.Value,EnvironmentVariableTarget.Process);
            }
            Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
            PreviewForm.RenderOnly=args.Contains("--render");
            using(var app=new PreviewLauncher()) {
                if(args.Contains("--verify")){app.Verify();var info=app.StartInfo();File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launcher-verification.json"),app.json.Serialize(new { verified=true,game_launched=false,private_home=info.Arguments,explicit_layer=info.EnvironmentVariables["XR_API_LAYER_PATH"],physical_runtime_override=info.EnvironmentVariables["XR_RUNTIME_JSON"] }));return 0;}
                if(args.Contains("--render")){app.SavePreview(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launcher-preview.png"));app.moreOptions.Visible=true;app.ClientSize=new Size(640,674);app.SavePreview(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launcher-options-preview.png"));return 0;}
                bool acquired;
                using(var single=new System.Threading.Mutex(true,@"Local\ETS2-VR-Preview-"+Hash(Encoding.UTF8.GetBytes(app.root.ToUpperInvariant())),out acquired)){
                    if(!acquired){MessageBox.Show("The launcher for this preview is already open.","ETS2 VR preview");return 0;}
                    try{Application.Run(app);}finally{single.ReleaseMutex();}
                }
            }return 0;
        }catch(Exception e){File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launcher-error.txt"),e.ToString());if(!args.Contains("--verify")&&!args.Contains("--render"))MessageBox.Show(e.Message,"ETS2 VR preview",MessageBoxButtons.OK,MessageBoxIcon.Information);return 1;}
    }
}
