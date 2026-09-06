// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

internal sealed class RequirementSnapshot {
    internal bool x64,ngx;
    internal int windowsBuild;
    internal string gpu="",driver="",runtimeError="",probeError="";
    internal uint vendor;
    internal ulong dedicatedBytes;
}

internal static class Requirements {
    internal const string MinimumDriver="616.64";
    // DXGI excludes driver reservations: a 16 GB card reports about 15.6 GB.
    // Allow up to 1 GB reserved; shared RAM and 12 GB cards never qualify.
    internal const ulong MinimumVram=15UL*1024*1024*1024;
    internal static string[] Errors(RequirementSnapshot s) {
        var errors=new List<string>();
        if(!s.x64||s.windowsBuild<22000)errors.Add("Windows 11, 64-bit is required.");
        if(s.vendor!=0x10de||!Regex.IsMatch(s.gpu??"",@"\bGeForce RTX 50\d\d\b",RegexOptions.IgnoreCase))
            errors.Add("Use an RTX 50-series GPU as the primary graphics adapter. Detected: "+(String.IsNullOrEmpty(s.gpu)?"unknown":s.gpu)+".");
        if(s.dedicatedBytes<MinimumVram)errors.Add("At least 16 GB of dedicated GPU memory is required. Shared system memory does not count.");
        Version driver;
        if(!Version.TryParse(s.driver,out driver)||driver<new Version(MinimumDriver))errors.Add("Install NVIDIA driver "+MinimumDriver+" or newer, then restart Windows.");
        if(!s.ngx)errors.Add("The NVIDIA neural-rendering driver component is missing or incomplete. Reinstall the NVIDIA driver.");
        if(!String.IsNullOrEmpty(s.runtimeError))errors.Add(s.runtimeError);
        if(!String.IsNullOrEmpty(s.probeError))errors.Add(s.probeError);
        return errors.ToArray();
    }
    internal static void Require(){var errors=Errors(Probe());if(errors.Length!=0)throw new IOException(String.Join(Environment.NewLine,errors));}

    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
    struct OsVersion {public uint size,major,minor,build,platform;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)]public string servicePack;}
    [DllImport("ntdll.dll",CharSet=CharSet.Unicode)]static extern int RtlGetVersion(ref OsVersion version);
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
    struct AdapterDesc {
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)]public string description;
        public uint vendor,device,subsys,revision;
        public UIntPtr video,system,shared;
        public uint luidLow;public int luidHigh;public uint flags;
    }
    [DllImport("dxgi.dll")]static extern int CreateDXGIFactory1(ref Guid iid,out IntPtr factory);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]delegate int EnumAdapter(IntPtr factory,uint index,out IntPtr adapter);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]delegate int GetDescription(IntPtr adapter,out AdapterDesc desc);
    static T Method<T>(IntPtr obj,int slot) where T:class {return Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(Marshal.ReadIntPtr(obj),slot*IntPtr.Size),typeof(T)) as T;}

    internal static string CheckRuntime(string path) {
        try {
            if(String.IsNullOrEmpty(path)||!Path.IsPathRooted(path)||!File.Exists(path))throw new IOException();
            if(path.IndexOf("fixture",StringComparison.OrdinalIgnoreCase)>=0||path.IndexOf("offline",StringComparison.OrdinalIgnoreCase)>=0)throw new IOException();
            if(new FileInfo(path).Length>1024*1024)throw new IOException();
            var data=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(path));
            var runtime=(Dictionary<string,object>)data["runtime"];
            var library=(string)runtime["library_path"];
            if(String.IsNullOrWhiteSpace(library))throw new IOException();
            if(!Path.IsPathRooted(library))library=Path.GetFullPath(Path.Combine(Path.GetDirectoryName(path),library));
            ReadX64(library);
            return "";
        } catch {return "Select an installed OpenXR runtime in your headset software (VDXR in Virtual Desktop), then check again.";}
    }
    static byte[] ReadX64(string path) {
        var file=new FileInfo(path);if(!file.Exists||file.Length<256||file.Length>256L*1024*1024)throw new IOException("Invalid 64-bit library.");
        var bytes=File.ReadAllBytes(path);PreviewFiles.X64(bytes,"Runtime library");return bytes;
    }
    // Inspect the PE export table as data. Do not load or initialize a driver in setup.
    internal static bool HasNgxExports(byte[] bytes) {
        try {
            PreviewFiles.X64(bytes,"NGX driver");int pe=BitConverter.ToInt32(bytes,60),optional=pe+24;
            if(BitConverter.ToUInt16(bytes,optional)!=0x20b)return false;
            int sections=BitConverter.ToUInt16(bytes,pe+6),table=optional+BitConverter.ToUInt16(bytes,pe+20);
            Func<uint,int> offset=rva=>{
                for(int i=0;i<sections;i++){int p=table+i*40;uint start=BitConverter.ToUInt32(bytes,p+12),size=BitConverter.ToUInt32(bytes,p+16);
                    if(rva>=start&&(ulong)rva<(ulong)start+size)return checked((int)(BitConverter.ToUInt32(bytes,p+20)+rva-start));}
                throw new IOException("Invalid export address.");
            };
            int exports=offset(BitConverter.ToUInt32(bytes,optional+112));uint count=BitConverter.ToUInt32(bytes,exports+24);
            if(count>100000)return false;int names=offset(BitConverter.ToUInt32(bytes,exports+32));var found=new HashSet<string>();
            for(uint i=0;i<count;i++){
                int p=offset(BitConverter.ToUInt32(bytes,checked(names+(int)i*4))),end=p;
                while(end<bytes.Length&&end-p<256&&bytes[end]!=0)end++;
                if(end==bytes.Length||end-p==256)return false;found.Add(System.Text.Encoding.ASCII.GetString(bytes,p,end-p));
            }
            return new[]{"Init_Ext","Init_ProjectID","AllocateParameters","GetCapabilityParameters","DestroyParameters","CreateFeature","EvaluateFeature","ReleaseFeature"}.All(n=>found.Contains("NVSDK_NGX_D3D12_"+n));
        }catch{return false;}
    }
    static string DriverVersion() {
        var exe=Path.Combine(Environment.SystemDirectory,"nvidia-smi.exe");
        if(!File.Exists(exe))exe=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"NVIDIA Corporation","NVSMI","nvidia-smi.exe");
        if(!File.Exists(exe))return "";
        using(var process=Process.Start(new ProcessStartInfo(exe,"--query-gpu=driver_version --format=csv,noheader,nounits"){
            UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true})) {
            var output=process.StandardOutput.ReadToEndAsync();var error=process.StandardError.ReadToEndAsync();
            if(!process.WaitForExit(8000)){try{process.Kill();}catch{}return "";}
            if(process.ExitCode!=0)return "";return output.Result.Split(new[]{'\r','\n'},StringSplitOptions.RemoveEmptyEntries).FirstOrDefault()??"";
        }
    }
    internal static RequirementSnapshot Probe() {
        var s=new RequirementSnapshot{x64=Environment.Is64BitOperatingSystem&&Environment.Is64BitProcess};
        try{var os=new OsVersion{size=(uint)Marshal.SizeOf(typeof(OsVersion))};if(RtlGetVersion(ref os)!=0)throw new IOException();s.windowsBuild=(int)os.build;}catch{s.probeError="Windows version could not be checked.";}
        IntPtr factory=IntPtr.Zero,adapter=IntPtr.Zero;
        try {
            var id=new Guid("770aae78-f26f-4dba-a829-253c83d1b387");Marshal.ThrowExceptionForHR(CreateDXGIFactory1(ref id,out factory));
            Marshal.ThrowExceptionForHR(Method<EnumAdapter>(factory,12)(factory,0,out adapter));AdapterDesc desc;
            Marshal.ThrowExceptionForHR(Method<GetDescription>(adapter,10)(adapter,out desc));
            s.gpu=desc.description;s.vendor=desc.vendor;s.dedicatedBytes=desc.video.ToUInt64();
        }catch{s.probeError="The primary graphics adapter could not be checked. Reconnect locally and reinstall the graphics driver if needed.";}
        finally{if(adapter!=IntPtr.Zero)Marshal.Release(adapter);if(factory!=IntPtr.Zero)Marshal.Release(factory);}
        try{s.driver=DriverVersion().Trim();}catch{s.driver="";}
        using(var machine=Microsoft.Win32.RegistryKey.OpenBaseKey(Microsoft.Win32.RegistryHive.LocalMachine,Microsoft.Win32.RegistryView.Registry64)) {
            try{using(var key=machine.OpenSubKey(@"SOFTWARE\NVIDIA Corporation\Global\NGXCore")){
                var dir=key==null?null:key.GetValue("FullPath") as string;
                if(!String.IsNullOrEmpty(dir)&&Regex.IsMatch(dir,@"^[A-Za-z]:\\"))s.ngx=HasNgxExports(ReadX64(Path.Combine(dir,"_nvngx.dll")));
            }}catch{s.ngx=false;}
            try{using(var key=machine.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1"))s.runtimeError=CheckRuntime(key==null?null:key.GetValue("ActiveRuntime") as string);}
            catch{s.runtimeError="OpenXR could not be checked. Select your headset runtime, then check again.";}
        }
        return s;
    }
}
