//////////////////////////////////////////////////////////
// Omnigen Project Setup
//////////////////////////////////////////////////////////

0) Install Git with LFS

1) Download and install Visual Studio 2019

2) Qt 5
- Download and unpack Qt5.14: Omnigen GDrive / Dev / Libs
- Add QT_PATH environment variable (ex: C:\Qt\Qt5.14.1\5.14.1\msvc2017_64)
- Download and install the newest (!) Qt Visual Studio Add-in: https://download.qt.io/development_releases/vsaddin/2.4.2/
- Configure Qt settings in Visual Studio: 
---Top toolbar -> Extensions -> Qt VS Tools -> Qt Options
---Add new Qt Version (name it: "Qt 5.14") and make sure the correct path is specified.

3) FBX SDK
- Download and install FBX SDK for VS2017 (binary compatible with VS2019)https://www.autodesk.com/developer-network/platform-technologies/fbx-sdk-2019-5
- Add FBX_SDK_PATH environment variable (ex: C:\Program Files\Autodesk\FBX\FBX SDK\)

4) TBB
- Download and install Intel Thread-Building Blocks for Windows: Omnigen GDrive / Dev / Libs
- Set Intel TBB environment variables by running <Program Files>\IntelSWTools\compilers_and_libraries_<version>\windows\tbb\bin\tbbvars.bat intel64 vs2019
- Add TBB_PATH environment variable (ex: C:\Program Files (x86)\IntelSWTools\compilers_and_libraries_2020\windows\tbb)

5) OpenSSL
- Download and install Win64 OpenSSL v1.1.1h (select 'copy binaries to /bin folder' during installation)
- https://slproweb.com/products/Win32OpenSSL.html
- Add OPENSSL enviroment variable (ex: C:\Program Files\OpenSSL-Win64)

6) Geometric Tools
- Download and unpack Geometric Tools: Omnigen GDrive / Dev / Libs
- Add GTE_PATH environment variable (ex: C:\dev\GTE)

7) NVidia Texture Tools 
- Download and install NVidia Texture Tools 3: https://developer.nvidia.com/gpu-accelerated-texture-compression
- Add NVTT_PATH environment variable (ex: C:\Program Files\NVIDIA Corporation\NVIDIA Texture Tools)

8) GLM & GLI
- Download and unpack: https://github.com/g-truc/glm https://github.com/g-truc/gli
- Add GLM_PATH and GLI_PATH environment variables (ex: C:\dev\glm-master\ etc)

9) Setup vcpkg
a) Clone the vcpkg repo https://github.com/Microsoft/vcpkg
b) From the root directory of the clone, run bootstrap-vcpkg.bat
c) From the root directory of the clone, run `vcpkg integrate install` from a console to integrate with VS
d) You can now use vcpkg to install libraries like this: From the root directory, run `vcpkg install "libname"` from a console
- vcpkg install libnoise:x64-windows
- vcpkg install gdal:x64-windows
- vcpkg install opencv:x64-windows
e) Optional: If you don't want to navigate to your installation directory each time you can add vcpkg to your PATH environment variable
f) Further instructions if needed: https://docs.microsoft.com/en-us/cpp/build/vcpkg?view=msvc-160

10) Install .NET Framework 4.6 (!) SDK (!) or newer: https://www.microsoft.com/en-us/download/details.aspx?id=53321

11) If you get a compilation error related to qobjectdefs.h:
- line 535 replace std::result_of with std::invoke_result