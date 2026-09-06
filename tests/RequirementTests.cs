// CPU/file tests. No graphics work, driver initialization or game launch.
using System;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;
internal static class RequirementTests {
    static int count;
    static void Check(bool value,string name){if(!value)throw new Exception(name);count++;}
    static RequirementSnapshot Good(){return new RequirementSnapshot{x64=true,windowsBuild=26100,gpu="NVIDIA GeForce RTX 5070 Ti",vendor=0x10de,dedicatedBytes=Requirements.MinimumVram,driver="616.64",ngx=true};}
    static void Reject(Action<RequirementSnapshot> change,string name){var s=Good();change(s);Check(Requirements.Errors(s).Length>0,name);}
    static byte[] Pe(){var b=new byte[4096];b[0]=77;b[1]=90;BitConverter.GetBytes(128).CopyTo(b,60);BitConverter.GetBytes(0x4550).CopyTo(b,128);BitConverter.GetBytes((ushort)0x8664).CopyTo(b,132);return b;}
    static int Main(){
        var root=Path.Combine(Path.GetTempPath(),"vr-requirements-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        try {
            Check(Requirements.Errors(Good()).Length==0,"exact minimum accepted");
            Reject(s=>s.x64=false,"32-bit rejected");Reject(s=>s.windowsBuild=19045,"Windows 10 rejected");
            Reject(s=>s.vendor=0x1002,"non-NVIDIA rejected");Reject(s=>s.gpu="NVIDIA GeForce RTX 4090","older RTX rejected");
            Reject(s=>s.gpu="NVIDIA RTX PRO 5000","unvalidated professional GPU rejected");
            Reject(s=>s.dedicatedBytes=12UL*1024*1024*1024,"12 GB rejected");Reject(s=>s.dedicatedBytes=Requirements.MinimumVram-1,"memory boundary enforced");
            Reject(s=>s.dedicatedBytes=0,"unknown memory rejected");Reject(s=>s.driver="616.63","older driver rejected");
            Reject(s=>s.driver="","missing driver rejected");Reject(s=>s.driver="unknown","malformed driver rejected");
            Reject(s=>s.ngx=false,"missing NGX rejected");Reject(s=>s.runtimeError="invalid runtime","broken runtime rejected");Reject(s=>s.probeError="inspection failed","inspection failure rejected");
            var newer=Good();newer.driver="617.10";newer.gpu="NVIDIA GeForce RTX 5090";newer.dedicatedBytes=32UL*1024*1024*1024;Check(Requirements.Errors(newer).Length==0,"newer supported configuration accepted");
            Check(Requirements.CheckRuntime(null)!="","absent OpenXR blocked");
            string runtime=Path.Combine(root,"runtime.json"),dll=Path.Combine(root,"runtime.dll");File.WriteAllBytes(dll,Pe());
            File.WriteAllText(runtime,"{broken");Check(Requirements.CheckRuntime(runtime)!="","malformed manifest blocked");
            File.WriteAllText(runtime,"{\"runtime\":{\"library_path\":\"missing.dll\"}}");Check(Requirements.CheckRuntime(runtime)!="","missing runtime DLL blocked");
            File.WriteAllText(runtime,"{\"runtime\":{\"library_path\":\"runtime.dll\"}}");Check(Requirements.CheckRuntime(runtime)=="","relative x64 runtime library accepted");
            var x86=Pe();x86[132]=0x4c;x86[133]=1;File.WriteAllBytes(dll,x86);Check(Requirements.CheckRuntime(runtime)!="","x86 runtime blocked");
            Check(!Requirements.HasNgxExports(new byte[3]),"truncated NGX rejected");Check(!Requirements.HasNgxExports(Pe()),"PE without exports rejected");
            var b=Pe();BitConverter.GetBytes((ushort)1).CopyTo(b,134);BitConverter.GetBytes((ushort)240).CopyTo(b,148);BitConverter.GetBytes((ushort)0x20b).CopyTo(b,152);
            BitConverter.GetBytes(0x1000u).CopyTo(b,264);BitConverter.GetBytes(0x1000u).CopyTo(b,404);BitConverter.GetBytes(3584u).CopyTo(b,408);BitConverter.GetBytes(512u).CopyTo(b,412);
            BitConverter.GetBytes(8u).CopyTo(b,536);BitConverter.GetBytes(0x1100u).CopyTo(b,544);
            var names=new[]{"Init_Ext","Init_ProjectID","AllocateParameters","GetCapabilityParameters","DestroyParameters","CreateFeature","EvaluateFeature","ReleaseFeature"};
            for(int i=0;i<names.Length;i++){BitConverter.GetBytes((uint)(0x1200+i*100)).CopyTo(b,768+i*4);Encoding.ASCII.GetBytes("NVSDK_NGX_D3D12_"+names[i]).CopyTo(b,1024+i*100);}
            Check(Requirements.HasNgxExports(b),"all eight driver exports found without loading DLL");b[1024]=0;Check(!Requirements.HasNgxExports(b),"missing required export rejected");
            Console.WriteLine("Passed "+count+" requirement checks. No game or graphics work ran.");return 0;
        }catch(Exception e){Console.Error.WriteLine(e);return 1;}
        finally{foreach(var p in Directory.GetFiles(root))File.Delete(p);Directory.Delete(root,false);}
    }
}
