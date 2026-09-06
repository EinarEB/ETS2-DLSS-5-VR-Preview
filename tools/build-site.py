"""Copy the editable page into docs, where the shared image assets live."""
from pathlib import Path
import shutil
root=Path(__file__).resolve().parents[1]
for name in ('index.html','app.js','style.css','setup.html','credits.html'):
    shutil.copy2(root/'site'/name,root/'docs'/name)
print('Updated docs. Preview with: python -m http.server --directory docs')
