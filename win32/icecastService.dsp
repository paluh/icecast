# Microsoft Developer Studio Project File - Name="icecastService" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=icecastService - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "icecastService.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "icecastService.mak" CFG="icecastService - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "icecastService - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "icecastService - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "icecastService - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "../" /I "../../libxslt/include" /I "../../curl/include" /I "../../iconv/include" /I "../../libxml2/include" /I "..\src" /I "..\src/httpp" /I "..\src/thread" /I "..\src/log" /I "..\src/avl" /I "..\src/net" /I "..\src/timings" /I "../../pthreads" /I "../../oggvorbis-win32sdk-1.0.1/include" /I "../../theora/include" /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /D "WIN32_SERVICE" /D "HAVE_CURL" /D "USE_YP" /D "HAVE_SYS_STAT_H" /D PACKAGE_VERSION=\"2.3.1\" /D "HAVE_THEORA" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib Releaseicecast\icecast.lib ..\..\curl\lib\Release\libcurl.lib ..\..\oggvorbis-win32sdk-1.0.1\lib\ogg_static.lib ..\..\oggvorbis-win32sdk-1.0.1\lib\vorbis_static.lib ..\..\libxml2\lib\libxml2.lib ..\..\libxslt\lib\libxslt.lib ..\..\iconv\lib\iconv.lib ..\..\pthreads\pthreadVSE.lib ws2_32.lib ..\..\theora\win32\Static_Release\theora_static.lib ../../speex/win32/libspeex/Release/libspeex.lib /nologo /subsystem:console /machine:I386 /nodefaultlib:"libc.lib"
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "icecastService - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "icecastService___Win32_Debug"
# PROP BASE Intermediate_Dir "icecastService___Win32_Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "icecastService___Win32_Debug"
# PROP Intermediate_Dir "icecastService___Win32_Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "../" /I "../../libxslt/include" /I "../../curl/include" /I "../../iconv/include" /I "../../libxml2/include" /I "..\src" /I "..\src/httpp" /I "..\src/thread" /I "..\src/log" /I "..\src/avl" /I "..\src/net" /I "..\src/timings" /I "../../pthreads" /I "../../oggvorbis-win32sdk-1.0.1/include" /I "../../theora/include" /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /D "WIN32_SERVICE" /D "HAVE_CURL" /D "USE_YP" /D "HAVE_SYS_STAT_H" /D PACKAGE_VERSION=\"2.3.1\" /D "HAVE_THEORA" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 Debugicecast\icecast.lib ..\..\curl\lib\Release\libcurl.lib ..\..\oggvorbis-win32sdk-1.0.1\lib\ogg_static.lib ..\..\oggvorbis-win32sdk-1.0.1\lib\vorbis_static.lib ..\..\libxml2\lib\libxml2.lib ..\..\libxslt\lib\libxslt.lib ..\..\iconv\lib\iconv.lib ..\..\pthreads\pthreadVSE.lib ws2_32.lib ..\..\theora\win32\Static_Debug\theora_static_d.lib ../../speex/win32/libspeex/Release/libspeex.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /nodefaultlib:"libcd.lib" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "icecastService - Win32 Release"
# Name "icecastService - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\icecastService.cpp
# End Source File
# Begin Source File

SOURCE=..\src\main.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
