// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

internal sealed class PreviewSetup : PreviewForm {
    static readonly string Package=AppDomain.CurrentDomain.BaseDirectory;
    readonly TextBox required=new TextBox(),gamePath=new TextBox(),destination=new TextBox();
    readonly Label status=new Label(),detail=new Label();
    readonly Button check=new Button(),install=new Button(),cancel=new Button(),open=new Button();
    readonly CheckBox shortcut=new CheckBox();
    readonly ListView items=new ListView();
    readonly ProgressBar progress=new ProgressBar();
    CancellationTokenSource cancellation;
    bool busy,closeWhenDone;
    string installedRoot,documents;
    static readonly Color Ink=Color.FromArgb(28,39,49),Muted=Color.FromArgb(82,97,109),Accent=Color.FromArgb(39,105,89);

    internal static string FindGame() {
        var roots=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var saved=Microsoft.Win32.Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam","SteamPath",null) as string;
        if(!String.IsNullOrEmpty(saved))roots.Add(saved);
        roots.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"Steam"));
        foreach(var root in roots.ToArray()) {
            var vdf=Path.Combine(root,"steamapps","libraryfolders.vdf");
            try{if(File.Exists(vdf)&&new FileInfo(vdf).Length<1024*1024)
                foreach(Match match in Regex.Matches(File.ReadAllText(vdf),"\"path\"\\s+\"([^\"]+)\""))roots.Add(match.Groups[1].Value.Replace("\\\\","\\"));
            }catch(IOException){}catch(UnauthorizedAccessException){}
        }
        foreach(var root in roots){var path=Path.Combine(root,"steamapps","common","Euro Truck Simulator 2","bin","win_x64","eurotrucks2.exe");if(File.Exists(path))return path;}
        return "";
    }
    internal static SetupInputs Discover(string folder,string game,string dest,string docs,bool desktop) {
        string[] files=Directory.Exists(folder)?Directory.GetFiles(folder):new string[0];
        Func<string,string> exact=name=>files.FirstOrDefault(p=>String.Equals(Path.GetFileName(p),name,StringComparison.OrdinalIgnoreCase));
        var reshade=exact("ReShade_Setup_6.8.0_Addon.exe")??exact("ReShade64.dll")??files.FirstOrDefault(p=>Regex.IsMatch(Path.GetFileName(p),@"^ReShade_Setup_6\.8\.0_Addon(?: \(\d+\))?\.exe$",RegexOptions.IgnoreCase));
        var snow=exact("ets2ats_lighting_v2_5_7_snowymoon.io.zip")??files.FirstOrDefault(p=>Regex.IsMatch(Path.GetFileName(p),@"^ets2ats_lighting_v2_5_7_snowymoon\.io(?: \(\d+\))?\.zip$",RegexOptions.IgnoreCase));
        return new SetupInputs{game_exe=game,reshade_dll=reshade??"",snowymoon_zip=snow??"",neural_folder=folder,destination=dest,documents=docs,desktop_shortcut=desktop};
    }
    static Button ButtonAt(string text,int x,int y,int width) {return new Button{Text=text,Location=new Point(x,y),Size=new Size(width,36),FlatStyle=FlatStyle.Flat,BackColor=Color.White,ForeColor=Ink};}
    Label LabelAt(string text,int x,int y,int width,int height,int size=10,bool bold=false) {
        var label=new Label{Text=text,Location=new Point(x,y),Size=new Size(width,height),ForeColor=Ink,Font=new Font("Segoe UI",size,bold?FontStyle.Bold:FontStyle.Regular)};Controls.Add(label);return label;
    }
    void BrowseField(TextBox box,string label,int y,bool file) {
        LabelAt(label,282,y,640,22,10,true);box.Location=new Point(282,y+24);box.Size=new Size(536,27);Controls.Add(box);
        var button=ButtonAt("Browse…",830,y+21,100);button.Click+=(s,e)=>{
            if(file){using(var dialog=new OpenFileDialog{Filter="ETS2 executable|eurotrucks2.exe",Title=label})if(dialog.ShowDialog(this)==DialogResult.OK){box.Text=dialog.FileName;SuggestDestination();}}
            else using(var dialog=new FolderBrowserDialog{Description=label})if(dialog.ShowDialog(this)==DialogResult.OK)box.Text=box==destination?Path.Combine(dialog.SelectedPath,"ETS2-VR-Preview"):dialog.SelectedPath;
        };Controls.Add(button);box.TextChanged+=(s,e)=>{if(!busy)install.Enabled=false;};
    }
    void SuggestDestination(){if(String.IsNullOrEmpty(destination.Text)&&!String.IsNullOrEmpty(gamePath.Text))destination.Text=Path.Combine(Path.GetPathRoot(gamePath.Text),"ETS2-VR-Preview");}
    void Open(string path){try{Process.Start(new ProcessStartInfo(path){UseShellExecute=true});}catch(Exception e){status.Text=e.Message;}}
    void Busy(bool value) {
        busy=value;check.Enabled=!value;install.Enabled=false;required.Enabled=!value;gamePath.Enabled=!value;destination.Enabled=!value;shortcut.Enabled=!value;
        cancel.Visible=value;cancel.Enabled=value;progress.Visible=value;progress.Style=ProgressBarStyle.Marquee;
        foreach(var button in Controls.OfType<Button>().Where(b=>b.Text=="Browse…"||b.Text=="Documents location…"))button.Enabled=!value;
    }
    SetupInputs Input(){return Discover(required.Text,gamePath.Text,destination.Text,documents,shortcut.Checked);}
    void ShowChecks(IEnumerable<InputCheck> checks) {
        items.Items.Clear();foreach(var result in checks){var row=new ListViewItem(result.name);row.SubItems.Add(result.message);row.ForeColor=result.ready?Accent:Color.FromArgb(145,66,39);items.Items.Add(row);}
    }
    static void CreateHandles(Control parent){var handle=parent.Handle;foreach(Control child in parent.Controls)CreateHandles(child);parent.PerformLayout();}
    async void CheckFiles(object sender,EventArgs args) {
        if(busy)return;SetupInputs input;try{input=Input();}catch(Exception e){status.Text=e.Message;return;}
        Busy(true);cancel.Visible=false;status.Text="Checking your downloads…";
        try {
            var results=await Task.Run(()=>SetupEngine.CheckInputs(input,Package));ShowChecks(results);
            var ready=results.All(r=>r.ready);Busy(false);install.Enabled=ready;
            status.Text=ready?"All required files match. Ready to prepare your preview.":"Some files need attention. Your game has not been changed.";
            detail.Text=ready?"Setup creates a separate game folder and launcher. It copies no campaigns.":"Open the download guide for the exact filenames and author links, then check again.";
        }catch(Exception e){Busy(false);status.Text="Could not complete the file check.";detail.Text=e.Message;}
        if(closeWhenDone)Close();
    }
    async void Install(object sender,EventArgs args) {
        if(busy)return;SetupInputs input;try{input=Input();}catch(Exception e){status.Text=e.Message;return;}
        Busy(true);cancellation=new CancellationTokenSource();status.Text="Checking the installation location…";detail.Text="You can cancel while setup prepares the new folder.";
        var reporter=new Progress<string>(message=>status.Text=message);
        try {
            var result=await Task.Run(()=>SetupEngine.Build(input,Package,message=>((IProgress<string>)reporter).Report(message),cancellation.Token));
            installedRoot=(string)result["renderer_root"];string shortcutMessage="The launcher is in your new preview folder.";
            if(input.desktop_shortcut)try{DesktopShortcut.Create(installedRoot);shortcutMessage="A desktop shortcut is ready.";}catch(Exception e){shortcutMessage="Installed successfully; the desktop shortcut could not be created. "+e.Message;}
            Busy(false);status.Text="Your preview is prepared.";detail.Text=shortcutMessage+" Open the launcher when you are ready to play.";
            open.Visible=true;open.Enabled=true;open.BringToFront();check.Enabled=false;install.Enabled=false;
        }catch(OperationCanceledException){Busy(false);status.Text="Setup cancelled.";detail.Text="The files created by this attempt were removed where possible. Your original game was not changed.";}
        catch(Exception e){Busy(false);status.Text="Setup stopped before completion.";detail.Text="Your original game was not changed. Any incomplete preview folder can be kept for diagnosis.";MessageBox.Show(this,e.Message,"Setup could not finish",MessageBoxButtons.OK,MessageBoxIcon.Information);}
        finally{cancellation.Dispose();cancellation=null;}
        if(closeWhenDone)Close();
    }
    PreviewSetup() {
        Text="ETS2 DLSS 5 VR Preview v0.1 — Setup";ClientSize=new Size(958,746);Font=new Font("Segoe UI",10);BackColor=Color.FromArgb(245,247,246);ForeColor=Ink;FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;StartPosition=FormStartPosition.CenterScreen;
        var sidebar=new Panel{BackColor=Ink,Location=Point.Empty,Size=new Size(250,746)};Controls.Add(sidebar);
        sidebar.Controls.Add(new Label{Text="ETS2\nDLSS 5 VR",Location=new Point(25,34),Size=new Size(210,98),ForeColor=Color.White,Font=new Font("Segoe UI",25,FontStyle.Bold)});
        sidebar.Controls.Add(new Label{Text="PREVIEW v0.1",Location=new Point(28,145),Size=new Size(195,27),ForeColor=Color.FromArgb(160,218,193),Font=new Font("Segoe UI",11,FontStyle.Bold)});
        sidebar.Controls.Add(new Label{Text="1   Add your downloads\n\n2   Prepare the preview\n\n3   Connect and drive",Location=new Point(28,234),Size=new Size(210,165),ForeColor=Color.White,Font=new Font("Segoe UI",11)});
        sidebar.Controls.Add(new Label{Text="Separate settings.\nYour regular game stays\navailable through Steam.\n\nFirst launch: create a new\nlocal profile for the preview.",Location=new Point(28,555),Size=new Size(205,145),ForeColor=Color.FromArgb(196,209,210),Font=new Font("Segoe UI",10)});
        LabelAt("A few downloads. One setup.",282,25,640,40,22,true);
        LabelAt("Add your own required files below. Setup checks their versions and prepares\na separate installation. It does not run the game or copy account files.",282,72,642,48);
        BrowseField(required,"Required files folder",130,false);
        var downloads=ButtonAt("Download guide ↗",282,190,192);downloads.Click+=(s,e)=>Open(Path.Combine(Package,"Read me first.html"));Controls.Add(downloads);
        var folder=ButtonAt("Open required files",486,190,192);folder.Click+=(s,e)=>{if(Directory.Exists(required.Text))Open(required.Text);else status.Text="Choose the included Required files folder.";};Controls.Add(folder);
        check.Text="Check files";check.Location=new Point(738,190);check.Size=new Size(192,36);check.FlatStyle=FlatStyle.Flat;check.Click+=CheckFiles;Controls.Add(check);
        items.Location=new Point(282,238);items.Size=new Size(648,158);items.View=View.Details;items.FullRowSelect=true;items.HeaderStyle=ColumnHeaderStyle.Nonclickable;items.Columns.Add("Component",260);items.Columns.Add("Status",378);items.BorderStyle=BorderStyle.FixedSingle;Controls.Add(items);
        BrowseField(gamePath,"ETS2 executable — detected from Steam",411,true);
        BrowseField(destination,"New preview folder — on the same drive as ETS2",478,false);
        documents=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),"Euro Truck Simulator 2");
        var docButton=ButtonAt("Documents location…",282,544,200);docButton.Click+=(s,e)=>{using(var dialog=new FolderBrowserDialog{Description="Your usual Euro Truck Simulator 2 Documents folder",SelectedPath=documents})if(dialog.ShowDialog(this)==DialogResult.OK){documents=dialog.SelectedPath;install.Enabled=false;}};Controls.Add(docButton);
        shortcut.Text="Create a desktop shortcut";shortcut.Checked=true;shortcut.Location=new Point(506,550);shortcut.Size=new Size(330,26);Controls.Add(shortcut);
        install.Text="Prepare preview";install.Location=new Point(282,596);install.Size=new Size(244,44);install.BackColor=Accent;install.ForeColor=Color.White;install.FlatStyle=FlatStyle.Flat;install.Enabled=false;install.Click+=Install;Controls.Add(install);
        cancel.Text="Cancel";cancel.Location=new Point(542,596);cancel.Size=new Size(105,44);cancel.Visible=false;cancel.Click+=(s,e)=>{if(cancellation!=null)cancellation.Cancel();status.Text="Stopping at a safe point…";cancel.Enabled=false;};Controls.Add(cancel);
        open.Text="Open launcher";open.Location=new Point(282,596);open.Size=new Size(244,44);open.Visible=false;open.BackColor=Accent;open.ForeColor=Color.White;open.FlatStyle=FlatStyle.Flat;open.Click+=(s,e)=>Open(Path.Combine(installedRoot,"ETS2 VR Preview.exe"));Controls.Add(open);
        progress.Location=new Point(664,610);progress.Size=new Size(266,10);progress.Visible=false;Controls.Add(progress);
        status.Location=new Point(282,653);status.Size=new Size(648,23);status.Font=new Font("Segoe UI",10,FontStyle.Bold);status.Text="Add the files listed in the guide, then choose Check files.";Controls.Add(status);
        detail.Location=new Point(282,682);detail.Size=new Size(648,52);detail.ForeColor=Muted;detail.Text="Snowymoon requires your own subscription or access from its author.";Controls.Add(detail);
        required.Text=Path.Combine(Package,"Required files");gamePath.Text=FindGame();SuggestDestination();
        FormClosing+=(s,e)=>{if(busy){closeWhenDone=true;e.Cancel=true;if(cancellation!=null)cancellation.Cancel();status.Text="Finishing the current check safely…";}};
    }
    [STAThread] static int Main(string[] args) {
        try {
            Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
            PreviewForm.RenderOnly=args.Contains("--render");
            if(args.Length==2&&(args[0]=="--check"||args[0]=="--prepare")){
                var input=new JavaScriptSerializer().Deserialize<SetupInputs>(File.ReadAllText(args[1]));
                if(args[0]=="--check")File.WriteAllText(Path.Combine(Package,"input-check.json"),new JavaScriptSerializer().Serialize(SetupEngine.CheckInputs(input,Package)));
                else {var prepared=SetupEngine.Build(input,Package,s=>{},CancellationToken.None);if(input.desktop_shortcut)DesktopShortcut.Create((string)prepared["renderer_root"]);}return 0;
            }
            using(var app=new PreviewSetup()){
                if(args.Contains("--render")){
                    app.required.Text=@"D:\Downloads\ETS2-VR-Preview\Required files";
                    app.gamePath.Text=@"D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2\bin\win_x64\eurotrucks2.exe";
                    app.destination.Text=@"D:\ETS2-VR-Preview";
                    CreateHandles(app);
                    app.ShowChecks(new[]{new InputCheck{name="ReShade 6.8 + add-on support",ready=true,message="Ready — tested version"},new InputCheck{name="Snowymoon Lighting 2.5.7",message="Missing — add the author's ZIP"},new InputCheck{name="Neural add-on and models",message="See download guide"}});
                    app.SavePreview(Path.Combine(Package,"setup-preview.png"));return 0;
                }
                Application.Run(app);
            }return 0;
        }catch(Exception e){File.WriteAllText(Path.Combine(Package,"setup-error.txt"),e.ToString());if(!args.Any(a=>a.StartsWith("--")))MessageBox.Show(e.Message,"ETS2 VR Preview setup");return 1;}
    }
}
