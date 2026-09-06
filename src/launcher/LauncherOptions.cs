// Copyright (c) 2026 ETS2 VR preview contributors. SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows.Forms;

internal sealed partial class PreviewLauncher {
    int activePasses;
    string activeQuality="";
    bool loadingOptions,starting;
    readonly ComboBox quality=new ComboBox(),look=new ComboBox(),style=new ComboBox(),model=new ComboBox(),toggleKey=new ComboBox();
    readonly NumericUpDown intensity=new NumericUpDown();
    readonly Label qualityStatus=new Label(),comparisonStatus=new Label();
    static readonly string[] QualityNames={"Custom","Low","Medium","High","Ultra"};
    static readonly int[][] QualityValues={new[]{1,50,60},new[]{1,65,75},new[]{2,80,90},new[]{2,100,0}};
    static readonly string[] LookFiles={"ETS2_VR_Preview_Clean.ini","ETS2_VR_Preview_Cooler.ini","ETS2_VR_Preview_ColdGrade.ini"};
    static readonly int[] ToggleKeys={145,19,0};
    int[] ReadQuality() {
        var values=PreviewFiles.ReadIni(File.ReadAllText(Path.Combine(root,"dlss5-feed.cfg")),"");
        Func<string,int> number=key=>{int result;if(!values.ContainsKey(key)||!Int32.TryParse(values[key],out result))throw new IOException("A quality setting is missing or invalid: "+key+". Prepare a fresh preview or restore its settings.");return result;};
        return new[]{number("stereo_passes"),number("work_resolution"),number("stereo_crop")};
    }
    int QualityIndex(int[] values){for(int i=0;i<QualityValues.Length;i++)if(values.SequenceEqual(QualityValues[i]))return i+1;return 0;}
    string DescribeQuality(int[] values){return QualityNames[QualityIndex(values)]+" · "+values[0]+(values[0]==1?" pass":" passes")+" · "+values[1]+"% resolution · "+(values[2]==0?"whole eye":values[2]+"% square");}
    void RefreshQuality() {
        var current=ReadQuality();var description=DescribeQuality(current);
        if(Active())qualityStatus.Text="Running: "+activeQuality+(description!=activeQuality?"\nRestart required — saved: "+description:"");
        else {loadingOptions=true;try{quality.SelectedIndex=QualityIndex(current);}finally{loadingOptions=false;}qualityStatus.Text="Next launch: "+description;}
    }
    bool CanChangeOptions(){return !loadingOptions&&!starting&&!Active()&&!settling&&System.Diagnostics.Process.GetProcessesByName("eurotrucks2").Length==0;}
    void SetOptionsEnabled(bool enabled){quality.Enabled=enabled;look.Enabled=enabled;style.Enabled=enabled;model.Enabled=enabled;intensity.Enabled=enabled;toggleKey.Enabled=enabled;}
    void SaveQuality(object sender,EventArgs args) {
        if(loadingOptions)return;
        if(!CanChangeOptions()){try{RefreshQuality();}catch{}status.Text="Close ETS2 before changing launcher settings.";return;}
        if(quality.SelectedIndex<1){RefreshQuality();return;}
        try {
            var selected=QualityValues[quality.SelectedIndex-1];var path=Path.Combine(root,"dlss5-feed.cfg");
            var updates=new Dictionary<string,string>{{"stereo_passes",selected[0].ToString()},{"work_resolution",selected[1].ToString()},{"stereo_crop",selected[2].ToString()},{"stereo_carrier_copy","1"},{"stereo_float_color","1"},{"work_composite","1"},{"work_upscale","0"}};
            var before=File.ReadAllText(path);File.Copy(path,Path.Combine(root,"quality-settings-before-change.cfg"),true);
            PreviewFiles.AtomicText(path,PreviewFiles.PatchIni(before,"",updates));RefreshQuality();
        }catch(Exception e){status.Text="Could not save quality. "+e.Message;try{RefreshQuality();}catch{};}
    }
    void RefreshAppearance() {
        loadingOptions=true;
        try {
            var vr=File.ReadAllText(Path.Combine(root,"ReShadeVR.ini"));var general=PreviewFiles.ReadIni(vr,"GENERAL");
            string preset;look.SelectedIndex=0;
            if(general.TryGetValue("PresetPath",out preset))for(int i=0;i<LookFiles.Length;i++)if(String.Equals(Path.GetFileName(preset),LookFiles[i],StringComparison.OrdinalIgnoreCase))look.SelectedIndex=i+1;
            var nr=PreviewFiles.ReadIni(File.ReadAllText(Path.Combine(root,"ReShade.ini")),"RenoDX.DLSS5");string value;int selected;decimal strength;
            style.SelectedIndex=nr.TryGetValue("NRStyle",out value)&&Int32.TryParse(value,out selected)&&selected>=0&&selected<=2?selected:1;
            model.SelectedIndex=nr.TryGetValue("NRPreset",out value)&&Int32.TryParse(value,out selected)&&selected>=0&&selected<=3?selected:1;
            intensity.Value=nr.TryGetValue("NRIntensity",out value)&&Decimal.TryParse(value,NumberStyles.Float,CultureInfo.InvariantCulture,out strength)&&strength>=0&&strength<=2?strength:2;
            var cfg=PreviewFiles.ReadIni(File.ReadAllText(Path.Combine(root,"dlss5-feed.cfg")),"");int key=145;
            if(cfg.TryGetValue("preview_toggle_key",out value))Int32.TryParse(value,out key);
            int index=Array.IndexOf(ToggleKeys,key);toggleKey.SelectedIndex=index>=0?index:2;
        }finally{loadingOptions=false;}
    }
    void SaveAppearance(object sender,EventArgs args) {
        if(loadingOptions)return;
        if(!CanChangeOptions()){try{RefreshAppearance();}catch{}status.Text="Close ETS2 before changing launcher settings.";return;}
        var before=new Dictionary<string,string>();var updated=new Dictionary<string,string>();var written=new List<string>();
        try {
            if(style.SelectedIndex<0||toggleKey.SelectedIndex<0)return;
            foreach(var name in new[]{"ReShade.ini","ReShadeVR.ini","dlss5-feed.cfg"})before[name]=File.ReadAllText(Path.Combine(root,name));
            var nr=new Dictionary<string,string>();
            if(sender==style)nr["NRStyle"]=style.SelectedIndex.ToString();
            if(sender==model&&model.SelectedIndex>=0)nr["NRPreset"]=model.SelectedIndex.ToString();
            if(sender==intensity)nr["NRIntensity"]=intensity.Value.ToString(CultureInfo.InvariantCulture);
            if(nr.Count>0)foreach(var name in new[]{"ReShade.ini","ReShadeVR.ini"})updated[name]=PreviewFiles.PatchIni(before[name],"RenoDX.DLSS5",nr);
            if(sender==look&&look.SelectedIndex>0){var relative=".\\Reshade presets\\"+LookFiles[look.SelectedIndex-1];if(!File.Exists(Path.Combine(root,relative)))throw new IOException("The selected color preset is missing.");updated["ReShadeVR.ini"]=PreviewFiles.PatchIni(before["ReShadeVR.ini"],"GENERAL",new Dictionary<string,string>{{"PresetPath",relative}});}
            if(sender==toggleKey)updated["dlss5-feed.cfg"]=PreviewFiles.PatchIni(before["dlss5-feed.cfg"],"",new Dictionary<string,string>{{"preview_toggle_key",ToggleKeys[toggleKey.SelectedIndex].ToString()}});
            if(updated.Count==0)return;
            var backup=Path.Combine(root,"settings-backups",DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff")+"-"+Guid.NewGuid().ToString("N").Substring(0,6));Directory.CreateDirectory(backup);
            foreach(var pair in before)File.Copy(Path.Combine(root,pair.Key),Path.Combine(backup,pair.Key));
            foreach(var pair in updated){PreviewFiles.AtomicText(Path.Combine(root,pair.Key),pair.Value);written.Add(pair.Key);}
            status.Text="Saved for your next launch.";RefreshAppearance();RefreshComparisonStatus();
        }catch(Exception e){
            bool restored=true;foreach(var name in written)try{PreviewFiles.AtomicText(Path.Combine(root,name),before[name]);}catch{restored=false;}
            status.Text="Could not save appearance. "+e.Message+(restored?"":" Restore the copies in settings-backups before launching.");
            if(!restored)launch.Enabled=false;try{RefreshAppearance();}catch{}
        }
    }
    void AddSelector(string title,ComboBox box,int x,int y,int width,string[] choices,Control parent=null) {
        parent=parent??this;
        parent.Controls.Add(new Label{Text=title,Location=new Point(x,y),Size=new Size(width,22),Font=new Font("Segoe UI",10,FontStyle.Bold)});
        box.DropDownStyle=ComboBoxStyle.DropDownList;box.Location=new Point(x,y+26);box.Size=new Size(width,29);box.Items.AddRange(choices);parent.Controls.Add(box);
        box.DrawMode=DrawMode.OwnerDrawFixed;box.ItemHeight=24;
        box.DrawItem+=(sender,args)=>{args.DrawBackground();if(args.Index>=0)TextRenderer.DrawText(args.Graphics,box.Items[args.Index].ToString(),box.Font,new Rectangle(args.Bounds.X+5,args.Bounds.Y,args.Bounds.Width-5,args.Bounds.Height),box.Enabled?args.ForeColor:SystemColors.GrayText,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);args.DrawFocusRectangle();};
    }
    void BuildOptions() {
        AddSelector("Quality",quality,28,105,276,QualityNames);
        AddSelector("Color look",look,324,105,288,new[]{"Custom","Clean","Cooler","Cold"});
        qualityStatus.Location=new Point(28,176);qualityStatus.Size=new Size(584,35);qualityStatus.ForeColor=Color.FromArgb(82,97,109);qualityStatus.Font=new Font("Segoe UI",9);Controls.Add(qualityStatus);
        AddSelector("Neural style",style,28,220,226,new[]{"Default","Natural","Cinematic"});
        AddSelector("Model",model,274,220,206,new[]{"Default","Preset 1","Preset 2","Preset 3"});
        Controls.Add(new Label{Text="Intensity",Location=new Point(500,220),Size=new Size(112,22),Font=new Font("Segoe UI",10,FontStyle.Bold)});
        intensity.Minimum=0;intensity.Maximum=2;intensity.DecimalPlaces=2;intensity.Increment=.1m;intensity.Location=new Point(500,246);intensity.Size=new Size(112,29);Controls.Add(intensity);
        AddSelector("Compare key",toggleKey,0,0,184,new[]{"Scroll Lock","Pause","Disabled"},moreOptions);
        var modelTip=new ToolTip();modelTip.SetToolTip(model,"The Classic add-on's model presets. Preset 1 is the default. The supplied model may use the same weights for more than one preset.");
        Controls.Add(new Label{Text="Changes apply at the next launch. Home opens live controls in game.",Location=new Point(28,300),Size=new Size(584,24),ForeColor=Color.FromArgb(82,97,109),Font=new Font("Segoe UI",9)});
        RefreshQuality();RefreshAppearance();quality.SelectedIndexChanged+=SaveQuality;look.SelectedIndexChanged+=SaveAppearance;style.SelectedIndexChanged+=SaveAppearance;model.SelectedIndexChanged+=SaveAppearance;intensity.ValueChanged+=SaveAppearance;toggleKey.SelectedIndexChanged+=SaveAppearance;
    }
}
