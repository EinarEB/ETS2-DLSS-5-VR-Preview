// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

internal static class DesktopShortcut {
    [ComImport,Guid("00021401-0000-0000-C000-000000000046")] class ShellLink {}
    [ComImport,Guid("000214F9-0000-0000-C000-000000000046"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IShellLinkW {
        void GetPath([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder file,int max,IntPtr data,uint flags);
        void GetIDList(out IntPtr id);void SetIDList(IntPtr id);
        void GetDescription([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder text,int max);
        void SetDescription([MarshalAs(UnmanagedType.LPWStr)]string text);
        void GetWorkingDirectory([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder text,int max);
        void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)]string path);
        void GetArguments([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder text,int max);
        void SetArguments([MarshalAs(UnmanagedType.LPWStr)]string text);
        void GetHotkey(out short key);void SetHotkey(short key);void GetShowCmd(out int cmd);void SetShowCmd(int cmd);
        void GetIconLocation([Out,MarshalAs(UnmanagedType.LPWStr)]StringBuilder path,int max,out int index);
        void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)]string path,int index);
        void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)]string path,uint reserved);
        void Resolve(IntPtr hwnd,uint flags);void SetPath([MarshalAs(UnmanagedType.LPWStr)]string path);
    }
    internal static string Create(string root) {
        var desktop=Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
        string target=Path.Combine(root,"ETS2 VR Preview.exe");if(!File.Exists(target))throw new IOException("The preview launcher is missing.");
        var name="ETS2 DLSS 5 VR Preview";var path=Path.Combine(desktop,name+".lnk");
        for(int i=2;File.Exists(path)&&i<=100;i++)path=Path.Combine(desktop,name+" ("+i+").lnk");
        if(File.Exists(path))throw new IOException("There are too many existing preview shortcuts.");
        var temporary=Path.Combine(desktop,"ets2-preview-"+Guid.NewGuid().ToString("N")+".lnk");var shell=new ShellLink();
        try {
            var link=(IShellLinkW)shell;link.SetPath(target);link.SetWorkingDirectory(root);link.SetDescription("Open the separate ETS2 DLSS 5 VR Preview launcher");link.SetIconLocation(target,0);link.SetShowCmd(1);
            ((IPersistFile)shell).Save(temporary,true);File.Move(temporary,path);return path;
        }finally{Marshal.FinalReleaseComObject(shell);if(File.Exists(temporary))File.Delete(temporary);}
    }
}
