using System;
using System.Drawing;
using System.Windows.Forms;

// Used only by the explicit --render developer command. Render a real form
// off screen without activating it, changing focus or starting a game.
internal class PreviewForm : Form {
    internal static bool RenderOnly;
    protected override bool ShowWithoutActivation { get{return RenderOnly||base.ShowWithoutActivation;} }
    protected override CreateParams CreateParams {
        get{var p=base.CreateParams;if(RenderOnly)p.ExStyle|=0x08000000|0x80;return p;}
    }
    internal void SavePreview(string path) {
        if(!RenderOnly)throw new InvalidOperationException("Rendering requires --render.");
        ShowInTaskbar=false;StartPosition=FormStartPosition.Manual;Location=new Point(-12000,-12000);
        Show();Application.DoEvents();PerformLayout();Refresh();
        try{using(var bitmap=new Bitmap(Width,Height)){DrawToBitmap(bitmap,new Rectangle(Point.Empty,bitmap.Size));bitmap.Save(path);}}
        finally{Hide();}
    }
}
