Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AdapterProbe {
 [DllImport("dxgi.dll")] static extern int CreateDXGIFactory2(uint flags,ref Guid iid,out IntPtr factory);
 [DllImport("d3d12.dll")] static extern int D3D12CreateDevice(IntPtr adapter,uint level,ref Guid iid,IntPtr output);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int EnumAdapter(IntPtr self,uint index,out IntPtr adapter);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int WarpAdapter(IntPtr self,ref Guid iid,out IntPtr adapter);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int Describe(IntPtr self,IntPtr description);
 static T Method<T>(IntPtr p,int slot) where T:class { return Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(Marshal.ReadIntPtr(p),slot*IntPtr.Size),typeof(T)) as T; }
 static void Probe(IntPtr a,string kind) {
  IntPtr desc=Marshal.AllocHGlobal(512);
  try {
   int hr=Method<Describe>(a,10)(a,desc); if(hr<0) Marshal.ThrowExceptionForHR(hr);
   string name=Marshal.PtrToStringUni(desc,128).TrimEnd('\0');
   Guid iid=new Guid("189819f1-1db6-4b57-be54-1821339b85f7");
   hr=D3D12CreateDevice(a,0xb000,ref iid,IntPtr.Zero);
   Console.WriteLine(kind+": "+name+"; D3D12 FL11_0="+(hr>=0)+"; HRESULT=0x"+hr.ToString("X8"));
  } finally {Marshal.FreeHGlobal(desc);Marshal.Release(a);}
 }
 public static void Run() {
  Guid iid=new Guid("1bc6ea02-ef36-464f-bf0c-21ca39e5168a"); IntPtr f;
  int hr=CreateDXGIFactory2(0,ref iid,out f); if(hr<0) Marshal.ThrowExceptionForHR(hr);
  try {
   for(uint i=0;;i++) {IntPtr a; hr=Method<EnumAdapter>(f,12)(f,i,out a); if(hr==unchecked((int)0x887a0002)) break; if(hr<0) Marshal.ThrowExceptionForHR(hr); Probe(a,"DXGI adapter");}
   Guid adapterIID=new Guid("29038f61-3839-4626-91fd-086879011a05"); IntPtr warp;
   hr=Method<WarpAdapter>(f,27)(f,ref adapterIID,out warp); if(hr<0) Marshal.ThrowExceptionForHR(hr); Probe(warp,"Explicit WARP");
  } finally {Marshal.Release(f);}
 }
}
'@
[AdapterProbe]::Run()
