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
    readonly Panel locations=new Panel(),actions=new Panel();
    readonly Button locationToggle=new Button();
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
    void BrowseField(TextBox box,string label,int y,bool file,Control parent=null) {
        parent=parent??this;
        parent.Controls.Add(new Label{Text=label,Location=new Point(0,y),Size=new Size(680,22),Font=new Font("Segoe UI",10,FontStyle.Bold)});box.Location=new Point(0,y+25);box.Size=new Size(588,27);parent.Controls.Add(box);
        var button=ButtonAt("Browse…",600,y+21,104);button.Click+=(s,e)=>{
            if(file){using(var dialog=new OpenFileDialog{Filter="ETS2 executable|eurotrucks2.exe",Title=label})if(dialog.ShowDialog(this)==DialogResult.OK){box.Text=dialog.FileName;SuggestDestination();}}
            else using(var dialog=new FolderBrowserDialog{Description=label})if(dialog.ShowDialog(this)==DialogResult.OK)box.Text=box==destination?Path.Combine(dialog.SelectedPath,"ETS2-VR-Preview"):dialog.SelectedPath;
        };parent.Controls.Add(button);box.TextChanged+=(s,e)=>{if(!busy)install.Enabled=false;};
    }
    void SuggestDestination(){if(String.IsNullOrEmpty(destination.Text)&&!String.IsNullOrEmpty(gamePath.Text))destination.Text=Path.Combine(Path.GetPathRoot(gamePath.Text),"ETS2-VR-Preview");}
    void Open(string path){try{Process.Start(new ProcessStartInfo(path){UseShellExecute=true});}catch(Exception e){status.Text=e.Message;}}
    void Busy(bool value) {
        busy=value;check.Enabled=!value;install.Enabled=false;required.Enabled=!value;gamePath.Enabled=!value;destination.Enabled=!value;shortcut.Enabled=!value;
        cancel.Visible=value;cancel.Enabled=value;progress.Visible=value;progress.Style=ProgressBarStyle.Marquee;
        foreach(var button in AllButtons(this).Where(b=>b.Text=="Browse…"||b.Text=="Documents location…"))button.Enabled=!value;
    }
    static IEnumerable<Button> AllButtons(Control parent) {
        foreach(Control child in parent.Controls){var button=child as Button;if(button!=null)yield return button;foreach(var nested in AllButtons(child))yield return nested;}
    }
    void ShowLocations(bool show) {
        AutoScrollPosition=Point.Empty;locations.Visible=show;locationToggle.Text=show?"Game and Documents folders −":"Game and Documents folders +";actions.Top=show?680:550;
        int height=show?822:692;AutoScroll=true;AutoScrollMinSize=new Size(0,height);
        ClientSize=new Size(768,RenderOnly?height:Math.Min(height,Math.Max(450,Screen.FromControl(this).WorkingArea.Height-80)));
    }
    SetupInputs Input(){return Discover(required.Text,gamePath.Text,destination.Text,documents,shortcut.Checked);}
    void ShowChecks(IEnumerable<InputCheck> checks) {
        items.Items.Clear();foreach(var result in checks){var row=new ListViewItem(result.name);row.SubItems.Add(result.message);row.ToolTipText=result.name+": "+result.message;row.ForeColor=result.ready?Accent:Color.FromArgb(145,66,39);items.Items.Add(row);}
    }
    static void CreateHandles(Control parent){var handle=parent.Handle;foreach(Control child in parent.Controls)CreateHandles(child);parent.PerformLayout();}
    async void CheckFiles(object sender,EventArgs args) {
        if(busy)return;SetupInputs input;try{input=Input();}catch(Exception e){status.Text=e.Message;return;}
        Busy(true);cancel.Visible=false;status.Text="Checking your downloads…";
        try {
            var results=await Task.Run(()=>SetupEngine.CheckInputs(input,Package));ShowChecks(results);
            var ready=results.All(r=>r.ready);Busy(false);install.Enabled=ready;
            if(results.Any(r=>r.name=="Euro Truck Simulator 2"&&!r.ready))ShowLocations(true);
            status.Text=ready?"All files ready. You can prepare the preview.":"Some files need attention.";
            detail.Text=ready?"Setup creates a separate preview with its own settings.":"Add the missing files, then check again.";
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
            open.Visible=true;open.Enabled=true;open.BringToFront();check.Enabled=false;install.Enabled=false;install.Visible=false;
        }catch(OperationCanceledException){Busy(false);status.Text="Setup cancelled.";detail.Text="The files created by this attempt were removed where possible. Your original game was not changed.";}
        catch(Exception e){Busy(false);status.Text="Setup stopped before completion.";detail.Text="Your original game was not changed. Any incomplete preview folder can be kept for diagnosis.";MessageBox.Show(this,e.Message,"Setup could not finish",MessageBoxButtons.OK,MessageBoxIcon.Information);}
        finally{cancellation.Dispose();cancellation=null;}
        if(closeWhenDone)Close();
    }
    PreviewSetup() {
        Text="ETS2 DLSS 5 VR Preview — Setup";ClientSize=new Size(768,692);Font=new Font("Segoe UI",10);BackColor=Color.FromArgb(245,247,246);ForeColor=Ink;FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;StartPosition=FormStartPosition.CenterScreen;
        LabelAt("Set up ETS2 VR",28,24,544,44,24,true);
        var version=LabelAt("PREVIEW 0.1",605,40,131,23,9,true);version.ForeColor=Accent;version.TextAlign=ContentAlignment.MiddleRight;
        var intro=LabelAt("Your downloads. One separate preview, ready to launch.",32,76,704,24);intro.ForeColor=Muted;
        var files=new Panel{Location=new Point(32,118),Size=new Size(704,57)};Controls.Add(files);BrowseField(required,"Required files folder",0,false,files);
        var downloads=ButtonAt("Download guide ↗",32,189,168);downloads.FlatAppearance.BorderSize=0;downloads.BackColor=BackColor;downloads.Click+=(s,e)=>Open(Path.Combine(Package,"Read me first.html"));Controls.Add(downloads);
        var folder=ButtonAt("Open folder",212,189,132);folder.FlatAppearance.BorderSize=0;folder.BackColor=BackColor;folder.Click+=(s,e)=>{if(Directory.Exists(required.Text))Open(required.Text);else status.Text="Choose the Required files folder first.";};Controls.Add(folder);
        check.Text="Check files";check.Location=new Point(568,189);check.Size=new Size(168,36);check.FlatStyle=FlatStyle.Flat;check.FlatAppearance.BorderColor=Color.FromArgb(192,204,198);check.Click+=CheckFiles;Controls.Add(check);
        items.Location=new Point(32,238);items.Size=new Size(704,164);items.View=View.Details;items.FullRowSelect=true;items.HeaderStyle=ColumnHeaderStyle.Nonclickable;items.Columns.Add("Component",292);items.Columns.Add("Status",386);items.BorderStyle=BorderStyle.FixedSingle;items.BackColor=Color.White;Controls.Add(items);
        var dest=new Panel{Location=new Point(32,420),Size=new Size(704,58)};Controls.Add(dest);BrowseField(destination,"Install location",0,false,dest);
        shortcut.Text="Create a desktop shortcut";shortcut.Checked=true;shortcut.Location=new Point(32,488);shortcut.Size=new Size(310,26);Controls.Add(shortcut);
        locationToggle.Text="Game and Documents folders +";locationToggle.Location=new Point(24,519);locationToggle.Size=new Size(292,28);locationToggle.FlatStyle=FlatStyle.Flat;locationToggle.FlatAppearance.BorderSize=0;locationToggle.TextAlign=ContentAlignment.MiddleLeft;locationToggle.Click+=(s,e)=>ShowLocations(!locations.Visible);Controls.Add(locationToggle);
        locations.Location=new Point(32,558);locations.Size=new Size(704,112);locations.Visible=false;Controls.Add(locations);BrowseField(gamePath,"ETS2 executable — detected from Steam",0,true,locations);
        documents=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),"Euro Truck Simulator 2");
        var docButton=ButtonAt("Documents location…",0,70,200);docButton.Click+=(s,e)=>{using(var dialog=new FolderBrowserDialog{Description="Your usual Euro Truck Simulator 2 Documents folder",SelectedPath=documents})if(dialog.ShowDialog(this)==DialogResult.OK){documents=dialog.SelectedPath;install.Enabled=false;}};locations.Controls.Add(docButton);
        actions.Location=new Point(32,550);actions.Size=new Size(704,130);Controls.Add(actions);
        install.Text="Prepare preview";install.Location=new Point(0,16);install.Size=new Size(246,44);install.BackColor=Accent;install.ForeColor=Color.White;install.FlatStyle=FlatStyle.Flat;install.FlatAppearance.BorderSize=0;install.Enabled=false;install.Click+=Install;actions.Controls.Add(install);
        cancel.Text="Cancel";cancel.Location=new Point(262,16);cancel.Size=new Size(105,44);cancel.Visible=false;cancel.FlatStyle=FlatStyle.Flat;cancel.Click+=(s,e)=>{if(cancellation!=null)cancellation.Cancel();status.Text="Stopping at a safe point…";cancel.Enabled=false;};actions.Controls.Add(cancel);
        open.Text="Open launcher";open.Location=new Point(0,16);open.Size=new Size(246,44);open.Visible=false;open.BackColor=Accent;open.ForeColor=Color.White;open.FlatStyle=FlatStyle.Flat;open.FlatAppearance.BorderSize=0;open.Click+=(s,e)=>Open(Path.Combine(installedRoot,"ETS2 VR Preview.exe"));actions.Controls.Add(open);
        progress.Location=new Point(436,34);progress.Size=new Size(268,8);progress.Visible=false;actions.Controls.Add(progress);
        status.Location=new Point(0,73);status.Size=new Size(704,23);status.Text="Add your downloads, then check the files.";actions.Controls.Add(status);
        detail.Location=new Point(0,102);detail.Size=new Size(704,28);detail.ForeColor=Muted;detail.Font=new Font("Segoe UI",9);detail.Text="Use your own Snowymoon subscription and model files.";actions.Controls.Add(detail);
        var tips=new ToolTip();items.ShowItemToolTips=true;status.AutoEllipsis=true;detail.AutoEllipsis=true;
        status.TextChanged+=(s,e)=>tips.SetToolTip(status,status.Text);detail.TextChanged+=(s,e)=>tips.SetToolTip(detail,detail.Text);
        install.EnabledChanged+=(s,e)=>{install.BackColor=install.Enabled?Accent:Color.FromArgb(218,226,222);};install.BackColor=Color.FromArgb(218,226,222);
        required.Text=Path.Combine(Package,"Required files");gamePath.Text=FindGame();SuggestDestination();ShowLocations(String.IsNullOrEmpty(gamePath.Text));
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
                    app.ShowLocations(false);
                    app.ShowChecks(new[]{new InputCheck{name="ReShade 6.8",ready=true,message="Ready"},new InputCheck{name="Snowymoon Lighting 2.5.7",message="Add the author's ZIP"},new InputCheck{name="Neural add-on",ready=true,message="Ready"},new InputCheck{name="DLSS model",ready=true,message="Ready"},new InputCheck{name="Neural rendering model",ready=true,message="Ready"},new InputCheck{name="ETS2 VR",ready=true,message="Ready"}});
                    app.SavePreview(Path.Combine(Package,"setup-preview.png"));app.ShowLocations(true);app.SavePreview(Path.Combine(Package,"setup-locations-preview.png"));return 0;
                }
                Application.Run(app);
            }return 0;
        }catch(Exception e){File.WriteAllText(Path.Combine(Package,"setup-error.txt"),e.ToString());if(!args.Any(a=>a.StartsWith("--")))MessageBox.Show(e.Message,"ETS2 VR Preview setup");return 1;}
    }
}
