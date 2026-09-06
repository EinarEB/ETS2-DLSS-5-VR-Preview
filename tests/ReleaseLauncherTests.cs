using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

// CPU/file-fixture tests only. Production Main, launch, Tick, Verify, shell and shortcut paths are never invoked.
internal static class ReleaseLauncherTests {
    static readonly BindingFlags Flags=BindingFlags.Instance|BindingFlags.NonPublic;
    static readonly Type T=typeof(PreviewLauncher);
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
    static readonly List<object> Checks=new List<object>(),Observations=new List<object>();
    static readonly string[] Configs={"ReShade.ini","ReShadeVR.ini","dlss5-feed.cfg"};
    static readonly string[] QualityKeys={"stereo_passes","work_resolution","stereo_crop","stereo_carrier_copy","stereo_float_color","work_composite","work_upscale"};
    static readonly int[][] Expected={new[]{1,50,60},new[]{1,65,75},new[]{2,80,90},new[]{2,100,0}};
    static readonly string[] Names={"Low","Medium","High","Ultra"};
    static string Base,Config; static Form App; static int Failures;
    static object Get(string n){return T.GetField(n,Flags).GetValue(App);}
    static void Set(string n,object value){T.GetField(n,Flags).SetValue(App,value);}
    static object Call(string n,params object[] args){try{return T.GetMethod(n,Flags).Invoke(App,args);}catch(TargetInvocationException e){throw e.InnerException;}}
    static void Check(bool yes,string name){Checks.Add(new{name=name,passed=yes});if(!yes)Failures++;}
    static void Case(string name,Action a){try{a();}catch(Exception e){Failures++;Checks.Add(new{name=name,passed=false,error=e.ToString()});}}
    static string Hash(byte[] b){using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(b)).Replace("-","").ToLowerInvariant();}
    static string P(string name){return Path.Combine(Base,name);}
    static void Write(string path,string text){path=Path.GetFullPath(path);if(!path.StartsWith(Base+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new Exception("Fixture write escapes test directory");Directory.CreateDirectory(Path.GetDirectoryName(path));File.WriteAllText(path,text,new UTF8Encoding(false));}
    static string FHash(string name){return Hash(File.ReadAllBytes(P(name)));}
    static Dictionary<string,string> Snapshot(){return Configs.ToDictionary(x=>x,x=>FHash(x));}
    static bool Same(Dictionary<string,string> b){return b.All(x=>FHash(x.Key)==x.Value);}
    static void Arrange(Action a){bool was=(bool)Get("loadingOptions");Set("loadingOptions",true);try{a();}finally{Set("loadingOptions",was);}}
    static void Select(string field,int value){Arrange(()=>((ComboBox)Get(field)).SelectedIndex=value);}
    static void Strength(decimal value){Arrange(()=>((NumericUpDown)Get("intensity")).Value=value);}
    static void SaveQuality(int index){Select("quality",index);Call("SaveQuality",Get("quality"),EventArgs.Empty);}
    static void SaveAppearance(string field){Call("SaveAppearance",Get(field),EventArgs.Empty);}
    static string Key(string line){return line.Split('=')[0].Trim();}
    static string[] OtherLines(string[] lines){return lines.Where(l=>!QualityKeys.Contains(Key(l),StringComparer.OrdinalIgnoreCase)).ToArray();}
    static string FullConfig(){return "; fixture comment retained exactly\r\nmodel=3\r\npreset=3\r\nNRPreset=2\r\nNRIntensity=1.37\r\ncustom_option = do=not=alter\r\nwork_mix=0.37\r\nstereo_second_structure=0.42\r\nstereo_stability=0.63\r\npreview_toggle_key=145\r\n\r\nstereo_passes=2\r\n stereo_passes = 1\r\nwork_resolution=72\r\nstereo_crop=66\r\nstereo_carrier_copy=0\r\nstereo_float_color=0\r\nwork_composite=0\r\nwork_upscale=1\r\n";}
    static string Desktop(){return "; desktop fixture\r\n[GENERAL]\r\nPresetPath=.\\Desktop stays put.ini\r\nUnrelatedDesktop=1\r\n[RenoDX.DLSS5]\r\nNRPreset=3\r\nNRIntensity=1.23\r\nNRStyle=2\r\nNRLocalToneStrength=0.62\r\nNRLocalStructureStrength=0.44\r\nNRSkinDetailStrength=0.81\r\nUnrelated=KEEP\r\n[OTHER]\r\nNRPreset=7\r\nNRIntensity=7\r\nNRStyle=7\r\n";}
    static string VR(){return "; headset fixture\r\n[GENERAL]\r\nPresetPath=.\\Reshade presets\\ETS2_VR_Preview_Cooler.ini\r\nUnrelatedVR=2\r\n[RenoDX.DLSS5]\r\nNRPreset=1\r\nNRIntensity=0.42\r\nNRStyle=0\r\nNRLocalToneStrength=0.13\r\nNRLocalStructureStrength=0.22\r\nNRSkinDetailStrength=0.33\r\nUnrelated=ALSO KEEP\r\n[OTHER]\r\nNRPreset=8\r\nNRIntensity=8\r\nNRStyle=8\r\n";}
    static void ResetConfigs(){Write(Config,FullConfig());Write(P("ReShade.ini"),Desktop());Write(P("ReShadeVR.ini"),VR());if(App!=null){Set("starting",false);Set("settling",false);Set("game",null);Call("RefreshAppearance");Call("RefreshQuality");}}
    // Independent canonical parser, used to require exactly selected semantic edits, including unrelated sections.
    static Dictionary<string,string> Values(string text){var r=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);string section="";foreach(var raw in text.Replace("\r\n","\n").Split('\n')){var l=raw.Trim();if(l.StartsWith("[")&&l.EndsWith("]")){section=l;continue;}int eq=l.IndexOf('=');if(eq>0&&!l.StartsWith(";")&&!l.StartsWith("#"))r[section+"/"+l.Substring(0,eq).Trim()]=l.Substring(eq+1).Trim();}return r;}
    static bool Exactly(string before,string after,params string[] changes){var a=Values(before);var b=Values(after);foreach(var c in changes){int eq=c.IndexOf('=');a[c.Substring(0,eq)]=c.Substring(eq+1);}return a.Count==b.Count&&a.All(x=>b.ContainsKey(x.Key)&&b[x.Key]==x.Value);}
    static int BackupCount(){return Directory.Exists(P("settings-backups"))?Directory.GetDirectories(P("settings-backups")).Length:0;}
    static string LatestBackup(){return Directory.GetDirectories(P("settings-backups")).OrderBy(p=>p,StringComparer.Ordinal).Last();}
    static void Observe(params int[][] rows){Write(P("native-input-observer.json"),Json.Serialize(new{actual_nr_parameters=rows.Select(r=>new{eye=r[0],pass=r[1],frame_id=r[2],result=r[3]}).ToArray()}));}
    static void Two(){Observe(new[]{0,1,10,1},new[]{1,1,10,1},new[]{0,2,10,1},new[]{1,2,10,1});}
    static void One(){Observe(new[]{0,1,10,1},new[]{1,1,10,1});}
    static bool Ready(){return (bool)Call("HasNeuralFrame");}
    static bool ThrowsQuality(){try{Call("ReadQuality");return false;}catch(IOException e){return e.Message.Contains("missing or invalid");}}
    static string Capture(){string p=P("capture");Directory.CreateDirectory(p);string[] kinds={"original","depth","motion","mask","result","work-color","work-depth","work-motion","work-result","work-mask"};for(int i=0;i<4;i++){var planes=new List<object>();foreach(string kind in kinds){string name="frame-"+i+"-"+kind+".raw";Write(Path.Combine(p,name),"12345678");planes.Add(new{file=name,kind=kind,width=2,height=1,bytes_per_pixel=4,bytes=8});}Write(Path.Combine(p,"frame-"+i+".json"),Json.Serialize(new{complete=true,comparison=true,index=i,layout="side_by_side",frame_id=20+i,images=planes}));}Write(Path.Combine(p,"session.json"),"{\"complete\":true,\"written\":4,\"requested\":4}");return p;}
    static bool Complete(string p){return (bool)Call("CaptureComplete",p,Json.Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(p,"session.json"))));}
    [STAThread] static int Main(){
        Base=Path.GetFullPath(AppDomain.CurrentDomain.BaseDirectory).TrimEnd(Path.DirectorySeparatorChar);Config=P("dlss5-feed.cfg");
        try {
            if(Process.GetProcessesByName("eurotrucks2").Length!=0)throw new Exception("Real ETS2 is running; fixture option-save checks cannot run without encountering the production global busy guard. No test action taken.");
            ResetConfigs();var original=P("source-documents/config.cfg");Write(original,"WORKSPACE-ONLY ORIGINAL CONFIG SENTINEL\r\n");
            foreach(var name in new[]{"Clean","Cooler","ColdGrade"})Write(P("Reshade presets/ETS2_VR_Preview_"+name+".ini"),"; inert fixture\n");
            var settings=new Dictionary<string,object>{{"renderer_root",Base},{"original_config",original},{"original_config_hash",FHash("source-documents/config.cfg")},{"game_exe",P("game-root/bin/win_x64/eurotrucks2.exe")},{"game_home",P("game-home")},{"layer_dir",P("layer")},{"checked_files",new Dictionary<string,object>()}};
            Write(P("preview.json"),Json.Serialize(settings));
            var immutable=new[]{"source-documents/config.cfg","preview.json"}.ToDictionary(x=>x,x=>FHash(x));
            var beforeLoad=Snapshot();var times=Configs.ToDictionary(x=>x,x=>File.GetLastWriteTimeUtc(P(x)).Ticks);
            var beforeBackupCount=BackupCount();var beforeQualityBackup=File.Exists(P("quality-settings-before-change.cfg"))?FHash("quality-settings-before-change.cfg"):null;
            App=(Form)Activator.CreateInstance(T,Flags,null,new object[0],null);
            Check(!App.Visible,"constructor never shows a window");Check(Same(beforeLoad),"initial load preserves all three config files byte-for-byte");Check(Configs.All(x=>File.GetLastWriteTimeUtc(P(x)).Ticks==times[x]),"initial load does not touch config modification times");Check(BackupCount()==beforeBackupCount&&(File.Exists(P("quality-settings-before-change.cfg"))?FHash("quality-settings-before-change.cfg"):null)==beforeQualityBackup,"initial load creates or changes no save backup, including a repeated fixture run");
            Check(((ComboBox)Get("quality")).SelectedIndex==0,"initial non-preset quality displays Custom");Check(((ComboBox)Get("look")).SelectedIndex==2,"initial VR look reads the VR preset independently of desktop");Check(((ComboBox)Get("style")).SelectedIndex==2&&((NumericUpDown)Get("intensity")).Value==1.23m,"initial style and intensity read the desktop RenoDX.DLSS5 section, with distinct VR values preserved");
            Check(((ComboBox)Get("model")).SelectedIndex==3,"initial model selection reads desktop NRPreset 3 while preserving distinct VR preset 1");
            Case("quality mapping",()=>{
                for(int i=0;i<4;i++){Write(Config,"stereo_passes="+Expected[i][0]+"\nwork_resolution="+Expected[i][1]+"\nstereo_crop="+Expected[i][2]+"\n");var q=(int[])Call("ReadQuality");Check(q.SequenceEqual(Expected[i]),Names[i]+" reads exact tuple");Check((int)Call("QualityIndex",(object)q)==i+1,Names[i]+" selector index correct");string text=Names[i]+" · "+q[0]+(q[0]==1?" pass":" passes")+" · "+q[1]+"% resolution · "+(q[2]==0?"whole eye":q[2]+"% square");Check((string)Call("DescribeQuality",(object)q)==text,Names[i]+" description reports actual tuple");}
                Write(Config,"; comment\n stereo_passes = 1\nwork_resolution=72\nstereo_crop=66\n");Check(((int[])Call("ReadQuality")).SequenceEqual(new[]{1,72,66}),"custom tuple retained");Check((int)Call("QualityIndex",(object)new[]{1,72,66})==0,"custom tuple is not coerced to a preset");
                Write(Config,"ignored=123\n");Check(ThrowsQuality(),"missing required quality key fails explicitly instead of using former Ultra default");Write(Config,"stereo_passes=bad\nwork_resolution=nan\nstereo_crop=invalid\n");Check(ThrowsQuality(),"malformed quality fails explicitly instead of defaulting");Write(Config,"STEREO_PASSES=2\n stereo_passes = 1\nwork_resolution=65\nstereo_crop=75\n");Check(((int[])Call("ReadQuality")).SequenceEqual(Expected[1]),"case-insensitive last duplicate quality value is read");
            });
            Case("quality saving and preservation",()=>{
                for(int i=0;i<4;i++){ResetConfigs();byte[] before=File.ReadAllBytes(Config);var lines=File.ReadAllLines(Config);var ini=new[]{"ReShade.ini","ReShadeVR.ini"}.ToDictionary(x=>x,x=>FHash(x));SaveQuality(i+1);var after=File.ReadAllLines(Config);Check(((int[])Call("ReadQuality")).SequenceEqual(Expected[i]),Names[i]+" save writes exact tuple");var nonqualityBefore=OtherLines(lines);var nonqualityAfter=OtherLines(after);if(!nonqualityBefore.SequenceEqual(nonqualityAfter))Observations.Add(new{case_name=Names[i]+" blank-line normalization",before_nonquality_lines=nonqualityBefore,after_nonquality_lines=nonqualityAfter});Check(nonqualityBefore.Where(x=>!String.IsNullOrWhiteSpace(x)).SequenceEqual(nonqualityAfter.Where(x=>!String.IsNullOrWhiteSpace(x))),Names[i]+" preserves unrelated nonblank lines and comments including their order/spacing, model, intensity, mix and structure");var vals=new[]{Expected[i][0],Expected[i][1],Expected[i][2],1,1,1,0};Check(QualityKeys.Select((k,j)=>after.Count(l=>String.Equals(Key(l),k,StringComparison.OrdinalIgnoreCase))==1&&after.Contains(k+"="+vals[j])).All(x=>x),Names[i]+" changes only seven quality keys and removes their duplicates");Check(Exactly(Encoding.UTF8.GetString(before),File.ReadAllText(Config),QualityKeys.Select((k,j)=>"/"+k+"="+vals[j]).ToArray()),Names[i]+" has exactly the seven intended semantic changes and no added unrelated setting");Check(FHash("quality-settings-before-change.cfg")==Hash(before),Names[i]+" backup has exact original bytes");Check(ini.All(x=>FHash(x.Key)==x.Value),Names[i]+" leaves both actual model/intensity INIs byte-identical");}
                ResetConfigs();var baseline=Snapshot();SaveQuality(0);Check(Same(baseline),"Custom selection performs no write");
                foreach(var field in new[]{"loadingOptions","starting","settling"}){Set(field,true);SaveQuality(2);Check(Same(baseline),field+" blocks a quality save");Set(field,false);}
                Set("game",Process.GetCurrentProcess());SaveQuality(2);Check(Same(baseline),"active session blocks quality save");Set("game",null);
                ResetConfigs();Select("quality",1);byte[] lockedBefore=File.ReadAllBytes(Config);bool escaped=false;using(var hold=new FileStream(Config,FileMode.Open,FileAccess.Read,FileShare.None)){try{Call("SaveQuality",Get("quality"),EventArgs.Empty);}catch{escaped=true;}}Check(!escaped,"locked quality file error remains inside event handler");Check(FHash("dlss5-feed.cfg")==Hash(lockedBefore),"locked quality save retains original bytes");Check(((Label)Get("status")).Text.StartsWith("Could not save quality."),"locked quality save reports its failure");Check(Directory.GetFiles(Base,"*.tmp-*").Length==0,"quality failure leaves no transaction temporary file");
                ResetConfigs();Select("quality",1);var replaceBefore=Snapshot();using(var hold=new FileStream(Config,FileMode.Open,FileAccess.Read,FileShare.Read)){Call("SaveQuality",Get("quality"),EventArgs.Empty);}Check(Same(replaceBefore),"quality replacement failure after successful read and backup retains all originals");Check(FHash("quality-settings-before-change.cfg")==replaceBefore["dlss5-feed.cfg"],"quality replacement failure still leaves an exact recovery backup");Check(((Label)Get("status")).Text.StartsWith("Could not save quality.")&&Directory.GetFiles(Base,"*.tmp-*").Length==0,"quality replacement failure reports status and cleans temporary file");
                ResetConfigs();int[] medium=Expected[1];Set("activePasses",1);Set("activeQuality",Call("DescribeQuality",(object)medium));Set("game",Process.GetCurrentProcess());Write(Config,"stereo_passes=2\nwork_resolution=80\nstereo_crop=90\n");Call("RefreshQuality");string label=((Label)Get("qualityStatus")).Text;Check(label.Contains("Running: Medium")&&label.Contains("Restart required — saved: High"),"running quality label retains launch snapshot and distinguishes saved High");Check((int)Get("activePasses")==1,"quality refresh never rewrites active pass snapshot");Set("game",null);
                ResetConfigs();int backups=BackupCount();((ComboBox)Get("quality")).SelectedIndex=2;Check(((int[])Call("ReadQuality")).SequenceEqual(Expected[1]),"actual quality selector event saves Medium without explicit handler call");Check(BackupCount()==backups,"quality selector does not invoke appearance transaction");
            });
            Case("model preset read and fallback",()=>{
                var selector=(ComboBox)Get("model");
                Check(selector.Items.Cast<object>().Select(x=>x.ToString()).SequenceEqual(new[]{"Default","Preset 1","Preset 2","Preset 3"}),"model selector exposes exact ordered labels for Classic NRPreset 0 through 3");
                foreach(var value in new[]{"missing","bad","-1","4"}) {
                    ResetConfigs();
                    string desktop=value=="missing"?Desktop().Replace("NRPreset=3\r\n",""):Desktop().Replace("NRPreset=3\r\n","NRPreset="+value+"\r\n");
                    Write(P("ReShade.ini"),desktop);var before=Snapshot();int backups=BackupCount();
                    Call("RefreshAppearance");
                    Check(selector.SelectedIndex==1,"model "+value+" uses the launcher's tested fallback 1");
                    Check(Same(before)&&BackupCount()==backups,"model "+value+" refresh leaves every config and backup unchanged");
                }
            });
            Case("model preset selected-control writes",()=>{
                for(int choice=0;choice<=3;choice++) {
                    ResetConfigs();
                    // Start from another saved preset so this is a genuine selector event for every value, including 3.
                    Write(P("ReShade.ini"),Desktop().Replace("NRPreset=3\r\n","NRPreset="+((choice+1)%4)+"\r\n"));
                    Call("RefreshAppearance");
                    string desktop=File.ReadAllText(P("ReShade.ini")),vr=File.ReadAllText(P("ReShadeVR.ini"));
                    var before=Snapshot();int backups=BackupCount();
                    int styleBefore=((ComboBox)Get("style")).SelectedIndex,lookBefore=((ComboBox)Get("look")).SelectedIndex,qualityBefore=((ComboBox)Get("quality")).SelectedIndex;
                    decimal strengthBefore=((NumericUpDown)Get("intensity")).Value;
                    ((ComboBox)Get("model")).SelectedIndex=choice;
                    Check(Exactly(desktop,File.ReadAllText(P("ReShade.ini")),"[RenoDX.DLSS5]/NRPreset="+choice),"model "+choice+" event changes only desktop RenoDX NRPreset, preserving style, intensity, look and unrelated section keys");
                    Check(Exactly(vr,File.ReadAllText(P("ReShadeVR.ini")),"[RenoDX.DLSS5]/NRPreset="+choice),"model "+choice+" event changes only VR RenoDX NRPreset, preserving divergent style, intensity, look and unrelated section keys");
                    Check(FHash("dlss5-feed.cfg")==before["dlss5-feed.cfg"],"model "+choice+" event keeps all quality, blend and feeder config bytes unchanged");
                    Check(((ComboBox)Get("model")).SelectedIndex==choice&&((ComboBox)Get("style")).SelectedIndex==styleBefore&&((ComboBox)Get("look")).SelectedIndex==lookBefore&&((ComboBox)Get("quality")).SelectedIndex==qualityBefore&&((NumericUpDown)Get("intensity")).Value==strengthBefore,"model "+choice+" refresh keeps all other displayed selections unchanged");
                    Check(BackupCount()==backups+1&&Configs.All(x=>Hash(File.ReadAllBytes(Path.Combine(LatestBackup(),x)))==before[x]),"model "+choice+" event makes one exact pre-edit backup set");
                }
                ResetConfigs();var unchanged=Snapshot();int count=BackupCount();Select("model",-1);SaveAppearance("model");
                Check(Same(unchanged)&&BackupCount()==count,"unset model selection cannot erase NRPreset or trigger an appearance transaction");
            });
            Case("model preset busy guards and disabled state",()=>{
                string[] options={"quality","look","style","model","intensity","toggleKey"};
                foreach(var busy in new[]{"starting","settling","active"}) {
                    ResetConfigs();Call("SetOptionsEnabled",true);var before=Snapshot();int backups=BackupCount();
                    if(busy=="active")Set("game",Process.GetCurrentProcess());else Set(busy,true);
                    try {
                        // Invoke only the common control gate used at launch/session completion, never LaunchGame or Tick.
                        Call("SetOptionsEnabled",false);
                        Check(options.All(x=>!((Control)Get(x)).Enabled),busy+" control gate disables model together with every other option");
                        ((ComboBox)Get("model")).SelectedIndex=0;
                        Check(Same(before)&&BackupCount()==backups,busy+" model event cannot write configs or create backups even if raised programmatically");
                        Check(((ComboBox)Get("model")).SelectedIndex==3&&((Label)Get("status")).Text.Contains("Close ETS2"),busy+" model event restores the saved selection and reports the busy state");
                    } finally {if(busy=="active")Set("game",null);else Set(busy,false);Call("SetOptionsEnabled",true);}
                }
                ResetConfigs();var initial=Snapshot();int initialBackups=BackupCount();Set("loadingOptions",true);
                try {((ComboBox)Get("model")).SelectedIndex=0;Check(Same(initial)&&BackupCount()==initialBackups,"loading model selection suppresses all save side effects");}
                finally {Set("loadingOptions",false);Call("RefreshAppearance");}
                Call("SetOptionsEnabled",true);Check(options.All(x=>((Control)Get(x)).Enabled),"idle control gate re-enables model and all other options");
            });
            Case("model preset partial-save rollback",()=>{
                ResetConfigs();var before=Snapshot();Select("model",0);int backups=BackupCount();
                using(var hold=new FileStream(P("ReShadeVR.ini"),FileMode.Open,FileAccess.Read,FileShare.Read)){SaveAppearance("model");}
                Check(Same(before),"locked second INI rolls back the first INI after a model edit without changing any original bytes");
                Check(BackupCount()==backups+1&&Configs.All(x=>Hash(File.ReadAllBytes(Path.Combine(LatestBackup(),x)))==before[x]),"failed model edit retains one exact recovery backup set");
                Check(((ComboBox)Get("model")).SelectedIndex==3&&((Label)Get("status")).Text.StartsWith("Could not save appearance."),"failed model edit restores its displayed saved value and reports the error");
            });
            Case("appearance selected-control edits",()=>{
                foreach(int choice in new[]{1,2,3}){ResetConfigs();var b=Snapshot();string oldvr=File.ReadAllText(P("ReShadeVR.ini"));Select("look",choice);SaveAppearance("look");string[] lookNames={"Clean","Cooler","ColdGrade"};string want=".\\Reshade presets\\ETS2_VR_Preview_"+lookNames[choice-1]+".ini";Check(Exactly(oldvr,File.ReadAllText(P("ReShadeVR.ini")),"[GENERAL]/PresetPath="+want),"color look "+lookNames[choice-1]+" changes only VR PresetPath");Check(FHash("ReShade.ini")==b["ReShade.ini"]&&FHash("dlss5-feed.cfg")==b["dlss5-feed.cfg"],"color look "+lookNames[choice-1]+" never changes desktop preset or other files");}
                ResetConfigs();var before=Snapshot();Select("look",0);int count=BackupCount();SaveAppearance("look");Check(Same(before)&&BackupCount()==count,"Current custom look makes no edit or backup");
                ResetConfigs();before=Snapshot();Select("look",3);string missing=P("Reshade presets/ETS2_VR_Preview_ColdGrade.ini");File.Move(missing,missing+".held");try{SaveAppearance("look");Check(Same(before),"missing requested VR preset changes no file");Check(((Label)Get("status")).Text.Contains("selected color preset is missing"),"missing VR preset reports explicit failure");}finally{File.Move(missing+".held",missing);}
                ResetConfigs();string desk=File.ReadAllText(P("ReShade.ini")),vr=File.ReadAllText(P("ReShadeVR.ini"));string cfg=FHash("dlss5-feed.cfg");Select("style",1);SaveAppearance("style");Check(Exactly(desk,File.ReadAllText(P("ReShade.ini")),"[RenoDX.DLSS5]/NRStyle=1"),"style edit changes only desktop NRStyle, retaining desktop preset and model/intensity");Check(Exactly(vr,File.ReadAllText(P("ReShadeVR.ini")),"[RenoDX.DLSS5]/NRStyle=1"),"style edit changes only VR NRStyle, retaining divergent intensity/model and VR preset");Check(FHash("dlss5-feed.cfg")==cfg,"style edit leaves quality/config bytes untouched");
                ResetConfigs();desk=File.ReadAllText(P("ReShade.ini"));vr=File.ReadAllText(P("ReShadeVR.ini"));cfg=FHash("dlss5-feed.cfg");Strength(0.91m);SaveAppearance("intensity");Check(Exactly(desk,File.ReadAllText(P("ReShade.ini")),"[RenoDX.DLSS5]/NRIntensity=0.91"),"intensity edit changes only desktop NRIntensity");Check(Exactly(vr,File.ReadAllText(P("ReShadeVR.ini")),"[RenoDX.DLSS5]/NRIntensity=0.91"),"intensity edit changes only VR NRIntensity, preserving divergent styles/model");Check(FHash("dlss5-feed.cfg")==cfg,"intensity edit leaves quality/config bytes untouched");
                for(int i=0;i<3;i++){ResetConfigs();before=Snapshot();string text=File.ReadAllText(Config);Select("toggleKey",i);SaveAppearance("toggleKey");int key=new[]{145,19,0}[i];Check(Exactly(text,File.ReadAllText(Config),"/preview_toggle_key="+key),"compare key choice "+i+" changes only preview_toggle_key");Check(FHash("ReShade.ini")==before["ReShade.ini"]&&FHash("ReShadeVR.ini")==before["ReShadeVR.ini"],"compare key choice "+i+" leaves both appearance INIs byte-identical");}
                ResetConfigs();before=Snapshot();count=BackupCount();Call("SaveAppearance",null,EventArgs.Empty);Check(Same(before)&&BackupCount()==count,"unrecognized appearance sender changes nothing");
                ResetConfigs();before=Snapshot();Select("style",1);count=BackupCount();SaveAppearance("style");string backup=LatestBackup();Check(BackupCount()==count+1,"one style operation makes exactly one backup set");Check(Configs.All(x=>Hash(File.ReadAllBytes(Path.Combine(backup,x)))==before[x]),"appearance backup retains all exact pre-edit config bytes");
                ResetConfigs();before=Snapshot();((ComboBox)Get("look")).SelectedIndex=1;Check(FHash("ReShade.ini")==before["ReShade.ini"]&&FHash("dlss5-feed.cfg")==before["dlss5-feed.cfg"]&&PreviewFiles.ReadIni(File.ReadAllText(P("ReShadeVR.ini")),"GENERAL")["PresetPath"].EndsWith("Clean.ini"),"actual look selector event changes only the VR look");
                ResetConfigs();desk=File.ReadAllText(P("ReShade.ini"));vr=File.ReadAllText(P("ReShadeVR.ini"));((NumericUpDown)Get("intensity")).Value=1.17m;Check(Exactly(desk,File.ReadAllText(P("ReShade.ini")),"[RenoDX.DLSS5]/NRIntensity=1.17")&&Exactly(vr,File.ReadAllText(P("ReShadeVR.ini")),"[RenoDX.DLSS5]/NRIntensity=1.17"),"actual intensity event writes only intensity in both INIs");
            });
            Case("appearance busy guards and rollback",()=>{
                foreach(var field in new[]{"loadingOptions","starting","settling"}){ResetConfigs();var b=Snapshot();Select("style",1);Set(field,true);SaveAppearance("style");Check(Same(b),field+" blocks an appearance save");Set(field,false);}
                ResetConfigs();var before=Snapshot();Select("style",1);Set("game",Process.GetCurrentProcess());SaveAppearance("style");Check(Same(before),"active session blocks appearance save");Set("game",null);
                // Read sharing lets all snapshots/backups succeed, but denies deletion/replacement of the second target.
                ResetConfigs();before=Snapshot();Select("style",1);int count=BackupCount();bool escaped=false;using(var hold=new FileStream(P("ReShadeVR.ini"),FileMode.Open,FileAccess.Read,FileShare.Read)){try{SaveAppearance("style");}catch{escaped=true;}}Check(!escaped,"second-file replacement failure stays within appearance handler");Check(Same(before),"second-file replacement failure rolls back the already-written desktop INI exactly");Check(BackupCount()==count+1,"partial appearance failure preserves one exact recovery backup set");string backup=LatestBackup();Check(Configs.All(x=>Hash(File.ReadAllBytes(Path.Combine(backup,x)))==before[x]),"partial appearance failure backup has exact original bytes for all files");Check(((Label)Get("status")).Text.StartsWith("Could not save appearance."),"partial appearance failure reports failure");Check(Directory.GetFiles(Base,"*.tmp-*").Length==0,"partial appearance failure cleans all replacement temporary files");
                ResetConfigs();before=Snapshot();Select("toggleKey",1);using(var hold=new FileStream(Config,FileMode.Open,FileAccess.Read,FileShare.Read)){SaveAppearance("toggleKey");}Check(Same(before),"locked config toggle edit retains all originals");Check(((Label)Get("status")).Text.StartsWith("Could not save appearance."),"locked toggle config reports failure");
                ResetConfigs();before=Snapshot();Select("style",1);count=BackupCount();using(var hold=new FileStream(Config,FileMode.Open,FileAccess.Read,FileShare.None)){SaveAppearance("style");}Check(Same(before)&&BackupCount()==count,"unreadable config prevents appearance writes before transaction begins");
            });
            Case("active pass readiness",()=>{
                Set("launchedAt",DateTime.UtcNow.AddSeconds(-2));Set("activePasses",2);string observer=P("native-input-observer.json");
                Check(!Ready(),"missing observer rejects readiness");Write(observer,"{broken");Check(!Ready(),"malformed observer rejects readiness");Two();Check(Ready(),"active two-pass snapshot accepts four successful same-frame slots");Write(Config,"stereo_passes=1\n");One();Check(!Ready(),"saved one-pass config cannot weaken active two-pass readiness");Two();Check(Ready(),"active two-pass remains ready despite saved one-pass change");
                Set("activePasses",1);Write(Config,"stereo_passes=2\n");One();Check(Ready(),"active one-pass accepts eye pair despite saved two-pass config");File.Delete(Config);Check(Ready(),"missing saved config does not alter active one-pass readiness");Write(Config,"stereo_passes=bad\n");Check(Ready(),"malformed saved config does not alter active one-pass readiness");Set("activePasses",2);Two();File.Delete(Config);Check(Ready(),"missing saved config does not alter active two-pass readiness");Write(Config,FullConfig());
                File.SetLastWriteTimeUtc(observer,DateTime.UtcNow.AddSeconds(-20));Check(!Ready(),"observer older than 15 seconds rejected");Two();Set("launchedAt",DateTime.UtcNow.AddSeconds(5));Check(!Ready(),"observer written before launch rejected");Set("launchedAt",DateTime.UtcNow.AddSeconds(-2));
                Observe(new[]{0,1,10,1},new[]{1,1,11,1},new[]{0,2,10,1},new[]{1,2,11,1});Check(!Ready(),"different eye frame IDs rejected");Observe(new[]{0,1,10,1},new[]{1,1,10,1},new[]{0,2,11,1},new[]{1,2,11,1});Check(!Ready(),"different pass frame IDs rejected");Observe(new[]{0,1,10,1},new[]{1,1,10,1},new[]{0,2,10,1},new[]{1,2,10,0});Check(!Ready(),"failed last eye pass rejected");Observe(new[]{0,1,0,1},new[]{1,1,0,1},new[]{0,2,0,1},new[]{1,2,0,1});Check(!Ready(),"zero frame IDs rejected");Observe(new[]{0,1,10,1},new[]{0,1,10,1},new[]{0,2,10,1},new[]{0,2,10,1});Check(!Ready(),"duplicate one-eye slots cannot fake stereo readiness");
                Two();Set("activePasses",0);Check(!Ready(),"uninitialized active pass count rejected");Set("activePasses",3);Check(!Ready(),"unsupported active pass count rejected");Set("activePasses",1);Observe(new[]{0,1,10,1},new[]{1,1,10,1},new[]{0,2,10,0},new[]{1,2,10,0});Check(Ready(),"one-pass readiness ignores unneeded failed second-pass slots");
            });
            Case("comparison status after exit",()=>{
                Set("game",null);var before=Snapshot();var label=(Label)Get("comparisonStatus");label.Text="Selected blend: 100% — waiting for a matching VR frame";
                Select("toggleKey",1);Call("RefreshComparisonStatus");Check(label.Text.Contains("Pause")&&!label.Text.Contains("waiting"),"an ended session clears stale frame status and shows the selected comparison key");
                Select("toggleKey",2);Call("RefreshComparisonStatus");Check(!label.Text.Contains("Pause")&&!label.Text.Contains("Scroll Lock"),"a disabled comparison key is not advertised while idle");Check(Same(before),"idle status refresh changes no settings");
            });
            Case("bounded capture metadata",()=>{string p=Capture();Check(Complete(p),"complete tiny four-frame ten-plane SBS capture accepted");Write(Path.Combine(p,"frame-3-result.raw"),"1234567");Check(!Complete(p),"truncated last-frame result rejected");Capture();Write(Path.Combine(p,"session.json"),"{\"complete\":false,\"written\":0,\"requested\":4}");Check(!Complete(p),"incomplete capture rejected");Capture();File.Delete(Path.Combine(p,"frame-3.json"));Check(!Complete(p),"missing fourth frame metadata rejected");});
            Check(immutable.All(x=>FHash(x.Key)==x.Value),"original-config and settings-manifest sentinels unchanged across entire suite");Check(!File.Exists(P("game-root/bin/win_x64/eurotrucks2.exe")),"no actual or fixture game executable was supplied");
        }catch(Exception e){Failures++;Observations.Add(new{fatal=e.ToString()});}
        finally{if(App!=null){Set("game",null);Set("starting",false);Set("settling",false);App.Dispose();}}
        Write(P("result.json"),Json.Serialize(new{passed=Failures==0,failed_checks=Failures,checks=Checks,observations=Observations,game_launched=false,forms_shown=false,fixture_root=Base,scope=new[]{"Unmodified production source copies compiled with this test entry point in one assembly.","No production Main, Verify, LaunchGame, Tick, CheckOriginal, restore, registry/hardware, Steam or shortcut paths invoked.","All writes stay inside the disposable workspace fixture directory.","Process.GetCurrentProcess is used only as an Active() sentinel; no child process is launched by this harness.","The production option busy guard enumerates eurotrucks2 process names read-only.","Most selector state is arranged with loadingOptions suppression before one explicit save; separate checks exercise actual selector events."}}));
        Console.WriteLine("Checks: "+Checks.Count+"; failed: "+Failures+"; observations: "+Observations.Count+". No game launched or form shown.");return Failures==0?0:1;
    }
}
