// File/CPU tests only. Fixtures contain dummy PE headers, never executable code.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

internal sealed class TestPlatform : SetupPlatform {
    internal long free=20L*1024*1024*1024;internal string format="NTFS";internal bool runtime=true;
    internal string[] errors=new string[0];
    internal override string[] SystemErrors(){return errors;}
    internal override string DriveFormat(string root){return format;}
    internal override long FreeBytes(string root){return free;}
    internal override bool RuntimePresent(string name){return runtime;}
}
internal static class SetupTests {
    static int checks;
    static readonly List<string> names=new List<string>();
    static void Check(bool ok,string name){checks++;names.Add(name);if(!ok)throw new Exception(name);}
    static void Reject(Action action,string name){bool rejected=false;try{action();}catch(IOException){rejected=true;}catch(OperationCanceledException){rejected=true;}Check(rejected,name);}
    static byte[] Pe(byte seed){var data=new byte[512];data[0]=77;data[1]=90;BitConverter.GetBytes(128).CopyTo(data,60);BitConverter.GetBytes(0x4550).CopyTo(data,128);BitConverter.GetBytes((ushort)0x8664).CopyTo(data,132);data[511]=seed;return data;}
    static void Write(string path,byte[] bytes){Directory.CreateDirectory(Path.GetDirectoryName(path));File.WriteAllBytes(path,bytes);}
    static void Text(string path,string text){Write(path,Encoding.UTF8.GetBytes(text));}
    static void Zip(string path,string entry,byte[] bytes){using(var file=File.Create(path))using(var archive=new ZipArchive(file,ZipArchiveMode.Create))using(var output=archive.CreateEntry(entry).Open())output.Write(bytes,0,bytes.Length);}
    static SetupInputs Copy(SetupInputs i,string destination){return new SetupInputs{game_exe=i.game_exe,reshade_dll=i.reshade_dll,snowymoon_zip=i.snowymoon_zip,neural_folder=i.neural_folder,destination=destination,documents=i.documents,desktop_shortcut=false};}
    static int Main(string[] args) {
        string root=Path.GetFullPath(Path.Combine(args.Length>0?args[0]:".","fixture-"+Guid.NewGuid().ToString("N").Substring(0,8)));
        Directory.CreateDirectory(root);
        try {
            string source=Path.Combine(root,"Game with spaces"),bin=Path.Combine(source,"bin","win_x64"),package=Path.Combine(root,"Release"),required=Path.Combine(root,"Required files"),docs=Path.Combine(root,"Documents ø"),dest=Path.Combine(root,"Preview ø");
            Directory.CreateDirectory(required);Directory.CreateDirectory(docs);Directory.CreateDirectory(package);
            foreach(var name in SetupEngine.NativeFiles)Write(Path.Combine(bin,name),Pe(1));
            Text(Path.Combine(source,"base.scs"),"synthetic archive only");Text(Path.Combine(source,"base_cfg.scs"),"second dummy archive");Text(Path.Combine(source,"steam_appid.vdf"),"fixture");
            var rs=Pe(2);Write(Path.Combine(required,"ReShade64.dll"),rs);Zip(Path.Combine(required,"snowymoon.zip"),"folder/dxgi.dll",Pe(3));
            var dependencies=new Dictionary<string,object>{{"eurotrucks2.exe",PreviewFiles.Hash(Pe(1))},{"ReShade64.dll",PreviewFiles.Hash(rs)},{"snowymoon-dxgi.dll",PreviewFiles.Hash(Pe(3))}};
            int seed=4;foreach(var name in SetupEngine.NeuralFiles){var bytes=Pe((byte)seed++);Write(Path.Combine(required,name),bytes);dependencies[name]=PreviewFiles.Hash(bytes);}
            var original="#keep\nuset r_scale_x \"2\"\nuset r_scale_y \"2\"\nuset r_mode_width \"1920\"\nuset unrelated_user_setting \"42\"\n";
            Text(Path.Combine(docs,"config.cfg"),original);Text(Path.Combine(docs,"global_controls.sii"),"synthetic controls");
            var payload=new Dictionary<string,object>();
            foreach(var pair in new Dictionary<string,string>{{"ETS2 VR Preview.exe","dummy launcher: never executed"},{"ReShade.ini","[GENERAL]\nPresetPath=.\\preset-desktop.ini\n[PROXY]\nProxyLibrary=@@SNOWY@@\nRoot=@@ROOT@@\n"},{"ReShadeVR.ini","[GENERAL]\nPresetPath=.\\Reshade presets\\ETS2_VR_Preview_Cooler.ini\nRoot=@@ROOT@@\n"},{"dlss5-feed.cfg","stereo_passes=1\nwork_resolution=65\nstereo_crop=75\n"},{"fx/test.fx","dummy shader: never compiled"}}){var path=Path.Combine(package,"payload",pair.Key);Text(path,pair.Value);payload[pair.Key]=PreviewFiles.Hash(path);}
            Action save=()=>Text(Path.Combine(package,"package-manifest.json"),new JavaScriptSerializer().Serialize(new{dependencies=dependencies,payload=payload}));save();
            var input=new SetupInputs{game_exe=Path.Combine(bin,"eurotrucks2.exe"),reshade_dll=Path.Combine(required,"ReShade64.dll"),snowymoon_zip=Path.Combine(required,"snowymoon.zip"),neural_folder=required,destination=dest,documents=docs,desktop_shortcut=false};
            var platform=new TestPlatform();
            Action<SetupInputs> validate=i=>{using(var p=SetupEngine.Validate(i,package,s=>{},CancellationToken.None,platform)){};};
            Check(SetupEngine.CheckInputs(input,package).All(c=>c.ready),"all six toy components validated");
            Check(PreviewFiles.Within(Path.Combine(root,"foo","bar"),Path.Combine(root,"foo")),"child containment");
            Check(!PreviewFiles.Within(Path.Combine(root,"foobar"),Path.Combine(root,"foo")),"sibling prefix rejected");
            Reject(()=>PreviewFiles.Child(root,"../escape"),"parent traversal rejected");Reject(()=>PreviewFiles.Child(root,"file:stream"),"alternate data stream rejected");
            var ini="[One]\na=1\nkeep=7\n[Two]\na=8\n";var patched=PreviewFiles.PatchIni(ini,"One",new Dictionary<string,string>{{"a","2"}});
            Check(PreviewFiles.ReadIni(patched,"One")["a"]=="2"&&PreviewFiles.ReadIni(patched,"One")["keep"]=="7","section patch preserves unrelated keys");
            Check(PreviewFiles.ReadIni(patched,"Two")["a"]=="8","section patch preserves other sections");
            Check(PreviewFiles.ReadIni(PreviewFiles.PatchIni(ini,"Missing",new Dictionary<string,string>{{"a","3"}}),"Missing")["a"]=="3","missing section appended");
            var atomic=Path.Combine(root,"atomic.ini");Text(atomic,"old");PreviewFiles.AtomicText(atomic,"new");Check(File.ReadAllText(atomic)=="new","atomic replacement");
            var held=Path.Combine(root,"held.ini");Text(held,"original");using(var locked=new FileStream(held,FileMode.Open,FileAccess.Read,FileShare.Read))Reject(()=>PreviewFiles.AtomicText(held,"new"),"locked config not overwritten");Check(File.ReadAllText(held)=="original","locked config preserved");
            var before=Directory.GetFileSystemEntries(root).Length;
            validate(input);Check(!Directory.Exists(dest)&&Directory.GetFileSystemEntries(root).Length==before,"validation writes nothing");
            Reject(()=>validate(Copy(input,source)),"existing game destination rejected");
            Reject(()=>validate(Copy(input,Path.Combine(source,"Preview"))),"destination inside game rejected");
            Reject(()=>validate(Copy(input,Path.Combine(docs,"Preview"))),"destination inside Documents rejected");
            Reject(()=>validate(Copy(input,Path.Combine(package,"Preview"))),"destination inside package rejected");
            var bad=Copy(input,dest);bad.game_exe=Path.Combine(root,"eurotrucks2.exe");Reject(()=>validate(bad),"wrong executable layout rejected");
            platform.errors=new[]{"Unsupported hardware"};Reject(()=>validate(input),"system requirement failure blocks setup");Check(!Directory.Exists(dest),"system rejection writes no destination");platform.errors=new string[0];
            platform.free=1024;Reject(()=>validate(input),"insufficient disk space rejected");platform.free=20L*1024*1024*1024;
            platform.format="exFAT";Reject(()=>validate(input),"non-NTFS rejected");platform.format="NTFS";
            platform.runtime=false;Reject(()=>validate(input),"missing VC runtime rejected");platform.runtime=true;
            var model=Path.Combine(required,"nvngx_dlssnr.dll");var modelBytes=File.ReadAllBytes(model);Write(model,Pe(90));Reject(()=>validate(input),"wrong model version rejected");Write(model,modelBytes);
            var malformed=Pe(1);malformed[132]=0x4c;malformed[133]=1;Reject(()=>PreviewFiles.X64(malformed,"test"),"x86 file rejected");
            Zip(Path.Combine(required,"wrong.zip"),"dxgi.dll",Pe(40));bad=Copy(input,dest);bad.snowymoon_zip=Path.Combine(required,"wrong.zip");Reject(()=>validate(bad),"wrong Snowymoon build rejected");
            var setupZip=Path.Combine(required,"rs.zip");Zip(setupZip,"ReShade64.dll",rs);var zipBytes=File.ReadAllBytes(setupZip);var sfx=new byte[1024+zipBytes.Length];Pe(1).CopyTo(sfx,0);zipBytes.CopyTo(sfx,1024);var sfxPath=Path.Combine(required,"ReShade_Setup_6.8.0_Addon.exe");Write(sfxPath,sfx);
            Check(PreviewFiles.Hash(SetupEngine.ReShade(sfxPath))==PreviewFiles.Hash(rs),"ReShade appended ZIP extraction without executing EXE");
            var cancelled=new CancellationTokenSource();cancelled.Cancel();Reject(()=>{using(var p=SetupEngine.Validate(input,package,s=>{},cancelled.Token,platform)){}},"pre-cancelled validation writes nothing");
            string failing=Path.Combine(root,"Failure rollback");
            using(var p=SetupEngine.Validate(Copy(input,failing),package,s=>{},CancellationToken.None,platform))Reject(()=>SetupEngine.Install(p,message=>{if(message.StartsWith("Preparing local"))throw new IOException("Injected failure after renderer copy");},CancellationToken.None),"mid-install injected failure");
            Check(!Directory.Exists(failing),"failed transaction removes only created files");Check(File.ReadAllText(Path.Combine(docs,"config.cfg"))==original,"failure preserves original config");
            var cancelRoot=Path.Combine(root,"Cancelled copy");var cancel=new CancellationTokenSource();
            using(var p=SetupEngine.Validate(Copy(input,cancelRoot),package,s=>{},CancellationToken.None,platform))Reject(()=>SetupEngine.Install(p,message=>{if(message.StartsWith("Preparing local"))cancel.Cancel();},cancel.Token),"mid-install cancellation");
            Check(!Directory.Exists(cancelRoot),"cancelled transaction rolls back");
            var lockedRoot=Path.Combine(root,"Locked rollback");var tx=new InstallTransaction(lockedRoot,CancellationToken.None);tx.Text("locked.txt","ours");
            using(var locked=new FileStream(Path.Combine(lockedRoot,"locked.txt"),FileMode.Open,FileAccess.Read,FileShare.Read)){tx.Dispose();Check(File.Exists(Path.Combine(lockedRoot,"setup-incomplete.txt")),"failed cleanup retains incomplete marker");}
            tx.Dispose();Check(!Directory.Exists(lockedRoot),"owned cleanup can finish after lock is released");
            var foreignRoot=Path.Combine(root,"Unrelated file");using(var owned=new InstallTransaction(foreignRoot,CancellationToken.None)){owned.Text("ours.txt","ours");Text(Path.Combine(foreignRoot,"theirs.txt"),"leave alone");}
            Check(File.ReadAllText(Path.Combine(foreignRoot,"theirs.txt"))=="leave alone"&&File.Exists(Path.Combine(foreignRoot,"setup-incomplete.txt")),"untracked file preserved with incomplete marker");
            Dictionary<string,object> installed;
            using(var p=SetupEngine.Validate(input,package,s=>{},CancellationToken.None,platform))installed=SetupEngine.Install(p,s=>{},CancellationToken.None);
            Check(File.Exists(Path.Combine(dest,"preview.json"))&&!File.Exists(Path.Combine(dest,"setup-incomplete.txt")),"successful install commits manifest and clears marker");
            Check(File.ReadAllText(Path.Combine(dest,"game-root","base.scs"))=="synthetic archive only","game archive shared");
            Check(PreviewFiles.Hash(Path.Combine(dest,"game-root/bin/win_x64/dxgi.dll"))==PreviewFiles.Hash(rs),"correct ReShade loader placed");
            Check(File.ReadAllText(Path.Combine(docs,"config.cfg"))==original,"successful install preserves original config");
            var config=File.ReadAllText(Path.Combine(dest,"game-home/Euro Truck Simulator 2/config.cfg"));
            Check(config.Contains("uset r_scale_x \"1\"")&&config.Contains("uset r_mode_width \"1920\"")&&config.Contains("unrelated_user_setting \"42\""),"preview baseline preserves display mode and other settings");
            Check(!Directory.EnumerateFileSystemEntries(Path.Combine(dest,"game-home/Euro Truck Simulator 2/profiles")).Any(),"no campaign or personal profile imported");
            Check(File.ReadAllText(Path.Combine(dest,"ReShadeVR.ini")).Contains("ETS2_VR_Preview_Cooler.ini")&&!File.ReadAllText(Path.Combine(dest,"ReShadeVR.ini")).Contains("@@ROOT@@"),"VR preset and root resolved independently");
            Reject(()=>validate(input),"repeat install never overwrites existing preview");
            if(args.Length>1)Check(PreviewFiles.Hash(SetupEngine.ReShade(args[1]))=="0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7","official ReShade6.8 installer extracts exact tested runtime without execution");
            var report=new{passed=checks,checks=names,game_launched=false,gpu_used=false,fixture=root};Text(Path.Combine(root,"results.json"),new JavaScriptSerializer().Serialize(report));
            Console.WriteLine("Passed "+checks+" CPU/file checks. "+root);return 0;
        }catch(Exception e){Text(Path.Combine(root,"failure.txt"),e.ToString());Console.Error.WriteLine(e);return 1;}
    }
}
