from PyInstaller.utils.hooks import collect_data_files
from PyInstaller.utils.hooks import copy_metadata
datas = []
# datas = [(r'E:/Bruce/Defector/Advantech','Advantech')]
datas += collect_data_files('pyzbar')
datas += collect_data_files('paddlex')
datas += collect_data_files('Cython')
datas += collect_data_files('Automation')
datas += collect_data_files('gxipy')

datas += copy_metadata('ftfy')
datas += copy_metadata('imagesize')
datas += copy_metadata('lxml')
datas += copy_metadata('opencv-contrib-python')
datas += copy_metadata('openpyxl')
datas += copy_metadata('premailer')
datas += copy_metadata('pyclipper')
datas += copy_metadata('pypdfium2')
datas += copy_metadata('scikit-learn')
datas += copy_metadata('shapely')
datas += copy_metadata('tokenizers')
datas += copy_metadata('einops')
datas += copy_metadata('jinja2')
datas += copy_metadata('regex')
datas += copy_metadata('tiktoken')

block_cipher = None
a = Analysis(
    ['main.py'],
    pathex=['.'],
    binaries=[(r'C:\Users\50190052\.conda\envs\defector\Lib\site-packages\paddle\libs', '.')],
    datas=datas,
    hiddenimports=['scipy._cyutility', 'opcua'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],  # 移除 fitz 和 PyMuPDF 的排除
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='Defector',  # 确保与你的项目名称一致
    debug=True,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=True,  # 保持控制台以查看错误
    icon='./zh1.ico'  # 这里指定图标文件
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='Defector'
)
