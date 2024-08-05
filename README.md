## Building Live555 library
1. clone https://github.com/melchi45/live555
2. cd live555
3. mkdir build
4. cd build
5. cmake .. -B vs2022 -G "Visual Studio 17 2022" -DLIVE555_ENABLE_OPENSSL=OFF -DLIVE555_BUILD_EXAMPLES=OFF -DLIVE555_MONOLITH_BUILD=ON<br/>
6. open generated VS solution that can be found in build folder and build the project
7. built dll can be found in build\vs2022\Debug

## Building the wrapper
1. clone this repository (Live555WrapperWin and live555 repositories should be in the same folder)
2. open VS solution
3. build

## Unity wrapper
1. copy live555.dll and Live555WrapperWin.dll into the Assets folder (can be placed in any subfolder)
2. create C# wrapper (example provided below)
```csharp
public static class Live555Wrapper
{
    [DllImport("Live555WrapperWin")]
    public static extern void CStart([MarshalAs(UnmanagedType.LPStr)] string url);

    [DllImport("Live555WrapperWin")]
    public static extern void CStop();

    [DllImport("Live555WrapperWin")]
    public static extern void CGetGazePoint(float[] gazePoint);

}
```
