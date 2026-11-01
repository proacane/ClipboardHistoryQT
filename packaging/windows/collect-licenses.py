"""Copy license materials from the exact Qt/MinGW kits used for this build."""
import argparse
from pathlib import Path
import shutil

parser = argparse.ArgumentParser()
parser.add_argument("package", type=Path)
parser.add_argument("--qt-docs", type=Path, required=True)
parser.add_argument("--mingw", type=Path, required=True)
args = parser.parse_args()
if not (args.package / "ClipboardHistory.exe").is_file():
    parser.error("package must be a deployed application directory")
destination = args.package / "licenses"
destination.mkdir(exist_ok=True)
for name in ("lgpl.html", "gpl.html", "licenses-used-in-qt.html"):
    shutil.copy2(args.qt_docs / "qtdoc" / name, destination / name)
count = 0
for module in ("qtcore", "qtgui", "qtqml", "qtquick", "qtquickcontrols",
               "qtquickdialogs", "qtnetwork", "qtsql", "qtsvg", "qtshadertools"):
    for source in (args.qt_docs / module).glob("*-attribution-*.html"):
        shutil.copy2(source, destination / source.name)
        count += 1
assert count > 40, "Qt attribution documentation is incomplete"
for component in ("gcc", "mingw-w64", "winpthreads"):
    shutil.copytree(args.mingw / "licenses" / component,
                    destination / "mingw" / component, dirs_exist_ok=True)
shutil.copy2(args.qt_docs.parent.parent / "Licenses" / "LICENSE.FDL", destination / "LICENSE.FDL")
shutil.copy2(Path(__file__).with_name("THIRD_PARTY_RUNTIME.md"), args.package / "THIRD_PARTY_NOTICES.md")
print(f"Copied Qt license texts, {count} attribution pages and MinGW runtime licenses.")
