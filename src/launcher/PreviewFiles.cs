// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

internal static class PreviewFiles {
    internal static string Full(string path) {
        if(String.IsNullOrWhiteSpace(path))throw new IOException("Choose a folder or file before continuing.");
        return Path.GetFullPath(path.Trim()).TrimEnd(Path.DirectorySeparatorChar);
    }
    internal static bool Within(string path,string parent,bool equal=false) {
        path=Full(path);parent=Full(parent);
        return (equal&&String.Equals(path,parent,StringComparison.OrdinalIgnoreCase)) ||
            path.StartsWith(parent+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase);
    }
    internal static void NoReparse(string path) {
        var current=Path.GetFullPath(path);
        while(!String.IsNullOrEmpty(current)) {
            if((File.Exists(current)||Directory.Exists(current))&&
                (File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0)
                throw new IOException("Use a regular local folder instead of a linked folder: "+current);
            current=Path.GetDirectoryName(current);
        }
    }
    internal static string Child(string root,string relative) {
        if(Path.IsPathRooted(relative)||relative.IndexOf(':')>=0)throw new IOException("A package path is not relative.");
        var path=Path.GetFullPath(Path.Combine(root,relative));
        if(!Within(path,root))throw new IOException("A package path escapes its folder.");
        NoReparse(path);return path;
    }
    internal static string Hash(string path) {using(var s=File.OpenRead(path))return Hash(s);}
    internal static string Hash(Stream stream) {using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(stream)).Replace("-","").ToLowerInvariant();}
    internal static string Hash(byte[] bytes) {using(var s=new MemoryStream(bytes,false))return Hash(s);}
    internal static void Expected(string path,string expected,string label) {
        if(!File.Exists(path))throw new IOException(label+" is missing. Open Required files for the download instructions.");
        if(!String.Equals(Hash(path),expected,StringComparison.OrdinalIgnoreCase))
            throw new IOException(label+" is a different version. Use the exact download listed in Required files; setup has not changed your game.");
    }
    internal static void X64(byte[] bytes,string label) {
        if(bytes.Length<128||bytes[0]!=77||bytes[1]!=90)throw new IOException(label+" is not a Windows program or DLL.");
        int offset=BitConverter.ToInt32(bytes,60);
        if(offset<64||offset>bytes.Length-24||BitConverter.ToUInt32(bytes,offset)!=0x4550||BitConverter.ToUInt16(bytes,offset+4)!=0x8664)
            throw new IOException(label+" must be the Windows 64-bit build.");
    }
    internal static void AtomicText(string path,string text) {
        NoReparse(path);
        string temporary=path+".tmp-"+Guid.NewGuid().ToString("N");
        try {
            File.WriteAllText(temporary,text,new UTF8Encoding(false));
            if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);
        } finally {if(File.Exists(temporary))File.Delete(temporary);}
    }
    // Section-aware, case-insensitive patching preserves unrelated settings and comments.
    internal static string PatchIni(string text,string section,IDictionary<string,string> updates) {
        var remaining=new Dictionary<string,string>(updates,StringComparer.OrdinalIgnoreCase);
        var result=new List<string>();bool inside=section.Length==0,found=inside;
        Action flush=()=>{foreach(var pair in remaining)result.Add(pair.Key+"="+pair.Value);remaining.Clear();};
        foreach(var line in text.Replace("\r\n","\n").Split('\n')) {
            var trimmed=line.Trim();
            if(trimmed.StartsWith("[")&&trimmed.EndsWith("]")) {
                if(inside)flush();
                inside=String.Equals(trimmed,"["+section+"]",StringComparison.OrdinalIgnoreCase);found|=inside;
                result.Add(line);continue;
            }
            int equals=trimmed.IndexOf('=');
            if(inside&&equals>0&&updates.Keys.Any(k=>String.Equals(k,trimmed.Substring(0,equals).Trim(),StringComparison.OrdinalIgnoreCase)))continue;
            result.Add(line);
        }
        if(!found){result.Add("");result.Add("["+section+"]");}
        flush();return String.Join(Environment.NewLine,result).TrimEnd()+Environment.NewLine;
    }
    internal static Dictionary<string,string> ReadIni(string text,string section) {
        var result=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);bool inside=section.Length==0;
        foreach(var line in text.Replace("\r\n","\n").Split('\n')) {
            var value=line.Trim();
            if(value.StartsWith("[")&&value.EndsWith("]")){inside=String.Equals(value,"["+section+"]",StringComparison.OrdinalIgnoreCase);continue;}
            int equals=value.IndexOf('=');if(inside&&equals>0&&!value.StartsWith(";")&&!value.StartsWith("#"))result[value.Substring(0,equals).Trim()]=value.Substring(equals+1).Trim();
        }
        return result;
    }
    internal static string PatchGame(string text,IDictionary<string,string> updates) {
        var remaining=new Dictionary<string,string>(updates,StringComparer.OrdinalIgnoreCase);var lines=new List<string>();
        foreach(var line in text.Replace("\r\n","\n").Split('\n')) {
            var words=line.Trim().Split(new[]{' ','\t'},StringSplitOptions.RemoveEmptyEntries);
            if(words.Length>=2&&words[0]=="uset"&&updates.ContainsKey(words[1]))continue;
            lines.Add(line);
        }
        foreach(var p in remaining)lines.Add("uset "+p.Key+" \""+p.Value+"\"");
        return String.Join(Environment.NewLine,lines).TrimEnd()+Environment.NewLine;
    }
}
