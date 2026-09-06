// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

internal sealed class SetupInputs {
    public string game_exe, reshade_dll, snowymoon_zip, neural_folder, destination, documents;
    public bool desktop_shortcut=true;
}
internal sealed class InputCheck {
    public string name,path,message;
    public bool ready;
}
internal class SetupPlatform {
    internal virtual string DriveFormat(string root){return new DriveInfo(root).DriveFormat;}
    internal virtual long FreeBytes(string root){return new DriveInfo(root).AvailableFreeSpace;}
    internal virtual bool RuntimePresent(string name){return File.Exists(Path.Combine(Environment.SystemDirectory,name));}
}
internal sealed class SetupPlan : IDisposable {
    internal SetupInputs input;
    internal string source,bin,regular,package;
    internal byte[] reshade,snowymoon;
    internal Dictionary<string,object> payload,dependencies;
    internal string[] archives;
    internal Dictionary<string,object> archiveState;
    internal readonly List<FileStream> locks=new List<FileStream>();
    internal void Hold(string path) {locks.Add(new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read));}
    public void Dispose(){foreach(var file in locks)file.Dispose();locks.Clear();}
}

// A transaction owns only the new destination and paths it creates with CreateNew.
// Rollback deletes those exact files; it never recursively deletes a directory.
internal sealed class InstallTransaction : IDisposable {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool CreateDirectory(string path,IntPtr security);
    internal readonly string Root;
    readonly List<string> files=new List<string>(),directories=new List<string>();
    readonly CancellationToken cancel;
    bool committed;
    internal InstallTransaction(string root,CancellationToken token) {
        Root=PreviewFiles.Full(root);cancel=token;PreviewFiles.NoReparse(Root);
        if(!Directory.Exists(Path.GetDirectoryName(Root)))throw new IOException("The preview's parent folder must already exist. Choose Browse to select a parent.");
        if(!CreateDirectory(Root,IntPtr.Zero))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"Could not create a new preview folder. Choose a writable location that does not already exist.");
        directories.Add(Root);
        try{Text("setup-incomplete.txt","Installation in progress. This is a new preview folder.\n");}catch{Dispose();throw;}
    }
    internal void Check(){cancel.ThrowIfCancellationRequested();PreviewFiles.NoReparse(Root);}
    internal string PathFor(string relative){Check();return PreviewFiles.Child(Root,relative);}
    internal void DirectoryFor(string path) {
        path=Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar);
        Check();if(!PreviewFiles.Within(path,Root,true))throw new IOException("Install destination escaped its root.");
        PreviewFiles.NoReparse(path);
        if(directories.Contains(path,StringComparer.OrdinalIgnoreCase))return;
        if(Directory.Exists(path)||File.Exists(path))throw new IOException("Another file appeared in the new installation. Setup stopped safely.");
        DirectoryFor(Path.GetDirectoryName(path));
        if(!CreateDirectory(path,IntPtr.Zero))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        directories.Add(path);
    }
    internal void Bytes(string relative,byte[] bytes) {
        var target=PathFor(relative);DirectoryFor(Path.GetDirectoryName(target));
        using(var output=new FileStream(target,FileMode.CreateNew,FileAccess.Write,FileShare.None)){
            files.Add(target);output.Write(bytes,0,bytes.Length);
        }
    }
    internal void Text(string relative,string text){Bytes(relative,new UTF8Encoding(false).GetBytes(text));}
    internal void Copy(string source,string relative) {
        var target=PathFor(relative);DirectoryFor(Path.GetDirectoryName(target));
        using(var input=new FileStream(source,FileMode.Open,FileAccess.Read,FileShare.Read))
        using(var output=new FileStream(target,FileMode.CreateNew,FileAccess.Write,FileShare.None)){
            files.Add(target);var buffer=new byte[131072];int count;
            while((count=input.Read(buffer,0,buffer.Length))>0){Check();output.Write(buffer,0,count);}
        }
    }
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool CreateHardLink(string target,string source,IntPtr unused);
    internal void Link(string source,string relative) {
        var target=PathFor(relative);DirectoryFor(Path.GetDirectoryName(target));
        if(!CreateHardLink(target,source,IntPtr.Zero))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(),"Could not share a game archive. The preview must be on the same NTFS drive as ETS2.");
        files.Add(target);
    }
    internal void Commit(){Check();File.Delete(Path.Combine(Root,"setup-incomplete.txt"));committed=true;}
    public void Dispose() {
        if(committed)return;
        // A cleanup failure leaves the incomplete folder for inspection; never
        // follow a replaced directory link or remove a file we did not create.
        bool incomplete=false;var marker=Path.Combine(Root,"setup-incomplete.txt");
        foreach(var file in files.AsEnumerable().Reverse()) {
            if(String.Equals(file,marker,StringComparison.OrdinalIgnoreCase))continue;
            try{if(PreviewFiles.Within(file,Root)){PreviewFiles.NoReparse(file);File.Delete(file);}}catch{incomplete=true;}
        }
        foreach(var dir in directories.AsEnumerable().Reverse()) {
            if(String.Equals(dir,Root,StringComparison.OrdinalIgnoreCase))continue;
            try{PreviewFiles.NoReparse(dir);Directory.Delete(dir,false);}catch{incomplete=true;}
        }
        if(incomplete)return;
        try{PreviewFiles.NoReparse(Root);File.Delete(marker);Directory.Delete(Root,false);}
        catch {
            // If an unrelated file arrived, leave a truthful marker beside it.
            try{PreviewFiles.NoReparse(Root);using(var f=new FileStream(marker,FileMode.CreateNew,FileAccess.Write)){
                var bytes=Encoding.UTF8.GetBytes("Setup did not finish. Some files remain in this new preview folder.\n");f.Write(bytes,0,bytes.Length);
            }}catch{}
        }
    }
}

internal static class SetupEngine {
    internal static readonly string[] NeuralFiles={"renodx-dlss5.addon64","nvngx_dlss.dll","nvngx_dlssnr.dll"};
    internal static readonly string[] NativeFiles={"eurotrucks2.exe","steam_api64.dll","fmod.dll","fmodstudio.dll","dstorage.dll","dstoragecore.dll","hidapi.dll","openvr_api.dll","thrustmaster_bridge_x64.dll","tobii_gameintegration_x64.dll"};
    static JavaScriptSerializer Json(){return new JavaScriptSerializer{MaxJsonLength=4000000};}
    static Dictionary<string,object> Map(object value){return (Dictionary<string,object>)value;}
    internal static Dictionary<string,object> Manifest(string package){return Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(package,"package-manifest.json")));}
    static byte[] ReadEntry(ZipArchive zip,string name,int limit) {
        var matches=zip.Entries.Where(e=>String.Equals(e.Name,name,StringComparison.OrdinalIgnoreCase)).ToArray();
        if(matches.Length!=1||matches[0].Length<128||matches[0].Length>limit)throw new IOException("The archive must contain exactly one valid "+name+".");
        using(var input=matches[0].Open())using(var output=new MemoryStream()){
            var buffer=new byte[65536];int count;
            while((count=input.Read(buffer,0,buffer.Length))>0){if(output.Length+count>limit)throw new IOException("An archive entry is too large.");output.Write(buffer,0,count);}
            if(output.Length!=matches[0].Length)throw new IOException("An archive entry is incomplete.");return output.ToArray();
        }
    }
    internal static byte[] Snowymoon(string path){using(var zip=ZipFile.OpenRead(path))return ReadEntry(zip,"dxgi.dll",32*1024*1024);}
    internal static byte[] ReShade(string path) {
        if(new FileInfo(path).Length>32*1024*1024)throw new IOException("The ReShade file is unexpectedly large.");
        if(!String.Equals(Path.GetExtension(path),".exe",StringComparison.OrdinalIgnoreCase))return File.ReadAllBytes(path);
        // ReShade's published setup appends its ZIP at a 512-byte boundary.
        // Read its data only. Neither setup EXE nor a supplied DLL is executed.
        var bytes=File.ReadAllBytes(path);if(bytes.Length>32*1024*1024)throw new IOException("The ReShade installer is unexpectedly large.");
        for(int offset=0;offset<=bytes.Length-30;offset+=512) {
            if(BitConverter.ToUInt32(bytes,offset)!=0x04034b50)continue;
            try{using(var memory=new MemoryStream(bytes,offset,bytes.Length-offset,false))using(var zip=new ZipArchive(memory,ZipArchiveMode.Read))return ReadEntry(zip,"ReShade64.dll",16*1024*1024);}
            catch(InvalidDataException){}
        }
        throw new IOException("Could not read the ReShade 6.8 add-on installer. Download the Add-on support version from the included link.");
    }
    internal static List<InputCheck> CheckInputs(SetupInputs input,string package) {
        var checks=new List<InputCheck>();var deps=Map(Manifest(package)["dependencies"]);
        Action<string,string,Func<byte[]>,string> add=(name,path,read,key)=>{
            var item=new InputCheck{name=name,path=path??"",ready=false};
            try{if(String.IsNullOrWhiteSpace(path)||!File.Exists(path))throw new IOException("Missing — add this download to Required files.");
                var bytes=read();PreviewFiles.X64(bytes,name);
                if(PreviewFiles.Hash(bytes)!=(string)deps[key])throw new IOException("Different version — see the exact version in the guide.");
                item.ready=true;item.message="Ready — tested version";
            }catch(Exception e){item.message=e.Message;}checks.Add(item);
        };
        add("Euro Truck Simulator 2",input.game_exe,()=>File.ReadAllBytes(input.game_exe),"eurotrucks2.exe");
        add("ReShade 6.8 + add-on support",input.reshade_dll,()=>ReShade(input.reshade_dll),"ReShade64.dll");
        add("Snowymoon Lighting 2.5.7",input.snowymoon_zip,()=>Snowymoon(input.snowymoon_zip),"snowymoon-dxgi.dll");
        foreach(var name in NeuralFiles){var local=name;var path=Path.Combine(input.neural_folder??"",local);add(local,path,()=>File.ReadAllBytes(path),local);}
        return checks;
    }
    internal static SetupPlan Validate(SetupInputs input,string package,Action<string> report,CancellationToken token,SetupPlatform platform=null) {
        platform=platform??new SetupPlatform();
        if(Process.GetProcessesByName("eurotrucks2").Length!=0)throw new IOException("Close ETS2 before installing the preview.");
        input=new SetupInputs{game_exe=PreviewFiles.Full(input.game_exe),reshade_dll=PreviewFiles.Full(input.reshade_dll),snowymoon_zip=PreviewFiles.Full(input.snowymoon_zip),neural_folder=PreviewFiles.Full(input.neural_folder),destination=PreviewFiles.Full(input.destination),documents=PreviewFiles.Full(input.documents),desktop_shortcut=input.desktop_shortcut};
        var plan=new SetupPlan{input=input,package=PreviewFiles.Full(package)};
        try {
            plan.bin=Path.GetDirectoryName(input.game_exe);
            if(!String.Equals(Path.GetFileName(input.game_exe),"eurotrucks2.exe",StringComparison.OrdinalIgnoreCase)||
                !String.Equals(Path.GetFileName(plan.bin),"win_x64",StringComparison.OrdinalIgnoreCase)||
                !String.Equals(Path.GetFileName(Path.GetDirectoryName(plan.bin)),"bin",StringComparison.OrdinalIgnoreCase))throw new IOException("Choose eurotrucks2.exe inside the game's bin / win_x64 folder.");
            plan.source=Path.GetDirectoryName(Path.GetDirectoryName(plan.bin));
            var dest=input.destination;
            if(Directory.Exists(dest)||File.Exists(dest))throw new IOException("Choose a new preview folder. Setup does not overwrite an existing installation.");
            PreviewFiles.NoReparse(dest);
            if(!Directory.Exists(Path.GetDirectoryName(dest)))throw new IOException("Choose an existing parent folder for the preview.");
            foreach(var protectedRoot in new[]{plan.source,input.documents,plan.package,input.neural_folder})
            {
                PreviewFiles.NoReparse(protectedRoot);
                if(PreviewFiles.Within(dest,protectedRoot,true)||PreviewFiles.Within(protectedRoot,dest,true))throw new IOException("Choose a preview folder separate from the game, Documents and downloaded files.");
            }
            if(dest.Length>140||Encoding.Default.GetString(Encoding.Default.GetBytes(dest))!=dest)throw new IOException("For this preview, choose a shorter installation path using characters supported by your Windows language. The default path is suitable.");
            if(!String.Equals(Path.GetPathRoot(dest),Path.GetPathRoot(plan.source),StringComparison.OrdinalIgnoreCase))throw new IOException("The preview must be on the same drive as ETS2.");
            var drive=Path.GetPathRoot(dest);
            if(platform.DriveFormat(drive)!="NTFS")throw new IOException("The game drive must use NTFS so setup can share its large archives.");
            if(platform.FreeBytes(drive)<8L*1024*1024*1024)throw new IOException("Keep at least 8 GB free on the game drive for setup, shaders and captures.");
            foreach(var name in new[]{"MSVCP140.dll","VCRUNTIME140.dll","VCRUNTIME140_1.dll"})
                if(!platform.RuntimePresent(name))throw new IOException("Install Microsoft's Visual C++ 2015–2022 x64 runtime first. The Required files guide links to it.");
            plan.regular=Path.Combine(input.documents,"config.cfg");
            if(!File.Exists(plan.regular))throw new IOException("No config.cfg was found in the selected ETS2 Documents folder. Start your usual game once or select the correct folder.");
            var manifest=Manifest(package);plan.dependencies=Map(manifest["dependencies"]);plan.payload=Map(manifest["payload"]);
            var protectedInputs=new[]{input.game_exe,input.reshade_dll,input.snowymoon_zip,plan.regular}.Concat(NeuralFiles.Select(n=>Path.Combine(input.neural_folder,n)));
            foreach(var path in protectedInputs){token.ThrowIfCancellationRequested();plan.Hold(path);}
            report("Checking the six required components…");
            foreach(var result in CheckInputs(input,package))if(!result.ready)throw new IOException(result.name+": "+result.message);
            plan.reshade=ReShade(input.reshade_dll);plan.snowymoon=Snowymoon(input.snowymoon_zip);
            var targets=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(var item in plan.payload){token.ThrowIfCancellationRequested();var path=PreviewFiles.Child(Path.Combine(package,"payload"),item.Key);
                if(!targets.Add(PreviewFiles.Child(dest,item.Key)))throw new IOException("The package repeats a destination.");
                plan.Hold(path);PreviewFiles.Expected(path,(string)item.Value,"Preview component "+Path.GetFileName(path));
            }
            plan.archives=Directory.GetFiles(plan.source,"*.scs",SearchOption.TopDirectoryOnly);
            if(plan.archives.Length==0||!File.Exists(Path.Combine(plan.source,"base.scs")))throw new IOException("The main game archives were not found.");
            plan.archiveState=new Dictionary<string,object>();
            foreach(var path in plan.archives){token.ThrowIfCancellationRequested();plan.Hold(path);var file=new FileInfo(path);plan.archiveState[path]=new Dictionary<string,object>{{"length",file.Length},{"modified",file.LastWriteTimeUtc.Ticks}};}
            foreach(var name in NativeFiles){var path=Path.Combine(plan.bin,name);if(!File.Exists(path))throw new IOException("ETS2 is missing "+name+". Verify the game in Steam.");if(path!=input.game_exe)plan.Hold(path);}
            return plan;
        } catch{plan.Dispose();throw;}
    }
    internal static Dictionary<string,object> Install(SetupPlan plan,Action<string> report,CancellationToken token) {
        var input=plan.input;var dest=input.destination;
        using(var tx=new InstallTransaction(dest,token)) {
            report("Sharing the game archives and preparing a separate renderer…");
            foreach(var path in plan.archives)tx.Link(path,"game-root/"+Path.GetFileName(path));
            foreach(var name in NativeFiles)tx.Copy(Path.Combine(plan.bin,name),"game-root/bin/win_x64/"+name);
            foreach(var path in Directory.GetFiles(plan.source,"*.vdf",SearchOption.TopDirectoryOnly))tx.Copy(path,"game-root/"+Path.GetFileName(path));
            tx.Bytes("game-root/bin/win_x64/dxgi.dll",plan.reshade);tx.Bytes("game-root/bin/win_x64/dxgi2.dll",plan.snowymoon);
            foreach(var name in NeuralFiles)tx.Copy(Path.Combine(input.neural_folder,name),name);
            foreach(var name in new[]{"nvngx_dlss.dll","nvngx_dlssnr.dll"})tx.Link(Path.Combine(dest,name),"game-root/bin/win_x64/"+name);
            foreach(var item in plan.payload) {
                if(item.Key.Equals("ReShade.ini",StringComparison.OrdinalIgnoreCase)||item.Key.Equals("ReShadeVR.ini",StringComparison.OrdinalIgnoreCase)){
                    var content=File.ReadAllText(Path.Combine(plan.package,"payload",item.Key)).Replace("@@ROOT@@",dest).Replace("@@SNOWY@@",Path.Combine(dest,"game-root/bin/win_x64/dxgi2.dll"));tx.Text(item.Key,content);
                }else tx.Copy(Path.Combine(plan.package,"payload",item.Key),item.Key);
            }
            report("Preparing local settings. Your campaigns stay where they are…");
            const string home="game-home/Euro Truck Simulator 2/";
            var defaults=new Dictionary<string,string>{{"r_manual_stereo_mirror_mode","3"},{"r_scale_x","1"},{"r_scale_y","1"},{"r_manual_stereo_buffer_scale","1.0"},{"r_dof","0"},{"r_aa","0"},{"r_multimon_mode","0"},{"r_taa_luma_sharpen","0"},{"r_taa_modulated_drr_strength","0.0"}};
            tx.Text(home+"config.cfg",PreviewFiles.PatchGame(File.ReadAllText(plan.regular),defaults));
            var controls=Path.Combine(input.documents,"global_controls.sii");if(File.Exists(controls))tx.Copy(controls,home+"global_controls.sii");
            tx.DirectoryFor(Path.Combine(dest,home,"profiles"));
            var bin=Path.Combine(dest,"game-root/bin/win_x64");
            tx.Text("layer/ReShade64_XR.json",Json().Serialize(new{file_format_version="1.0.0",api_layer=new{name="XR_APILAYER_reshade",library_path=Path.Combine(bin,"dxgi.dll"),api_version="1.0",implementation_version="2",description="ETS2 VR preview - local ReShade layer"}}));
            tx.Text("layer/ReShadeApps.ini","[GENERAL]\nApps="+Path.Combine(bin,"eurotrucks2.exe")+"\n");
            tx.Text("game-root/bin/win_x64/ReShade.ini","[INSTALL]\nBasePath="+dest+"\n");
            foreach(var folder in new[]{"cache","screenshots","DLSS5-Captures"})tx.DirectoryFor(Path.Combine(dest,folder));
            report("Verifying the prepared files…");
            var checkedFiles=new Dictionary<string,object>();
            foreach(var item in plan.payload)if(!item.Key.EndsWith(".ini",StringComparison.OrdinalIgnoreCase)&&!item.Key.EndsWith(".cfg",StringComparison.OrdinalIgnoreCase)){
                PreviewFiles.Expected(Path.Combine(dest,item.Key),(string)item.Value,"Installed "+Path.GetFileName(item.Key));checkedFiles[item.Key]=item.Value;
            }
            foreach(var name in NativeFiles){string relative="game-root/bin/win_x64/"+name;var hash=PreviewFiles.Hash(Path.Combine(plan.bin,name));PreviewFiles.Expected(Path.Combine(dest,relative),hash,name);checkedFiles[relative]=hash;}
            foreach(var entry in new[]{new[]{"game-root/bin/win_x64/dxgi.dll","ReShade64.dll"},new[]{"game-root/bin/win_x64/dxgi2.dll","snowymoon-dxgi.dll"}}){PreviewFiles.Expected(Path.Combine(dest,entry[0]),(string)plan.dependencies[entry[1]],entry[1]);checkedFiles[entry[0]]=plan.dependencies[entry[1]];}
            foreach(var name in NeuralFiles){PreviewFiles.Expected(Path.Combine(dest,name),(string)plan.dependencies[name],name);checkedFiles[name]=plan.dependencies[name];}
            foreach(var name in new[]{"nvngx_dlss.dll","nvngx_dlssnr.dll"})checkedFiles["game-root/bin/win_x64/"+name]=plan.dependencies[name];
            var result=new Dictionary<string,object>{{"schema",3},{"version","0.1-candidate21"},{"renderer_root",dest},{"game_exe",Path.Combine(bin,"eurotrucks2.exe")},{"game_home",Path.Combine(dest,"game-home")},{"layer_dir",Path.Combine(dest,"layer")},{"original_config",plan.regular},{"source_exe",input.game_exe},{"source_exe_hash",plan.dependencies["eurotrucks2.exe"]},{"source_archives",plan.archiveState},{"checked_files",checkedFiles}};
            tx.Text("preview.json",Json().Serialize(result));
            tx.Text("setup-complete.txt","Ready. Open ETS2 VR Preview.exe. On first launch, create a NEW LOCAL profile with Steam Cloud unchecked.\n");
            tx.Commit();return result;
        }
    }
    internal static Dictionary<string,object> Build(SetupInputs input,string package,Action<string> report,CancellationToken token) {
        using(var plan=Validate(input,package,report,token))return Install(plan,report,token);
    }
}
