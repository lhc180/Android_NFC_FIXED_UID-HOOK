#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include "config.h"
#include "injector.h"
#include "ptrace.h"
#include "utils.h"
#include <sys/system_properties.h>

static int android_os_version = -1;

int GetOSVersion()
{
  if (android_os_version != -1) {
    return android_os_version;
  }

  char os_version[PROP_VALUE_MAX + 1];
  int os_version_length = __system_property_get("ro.build.version.release", os_version);
  android_os_version = atoi(os_version);

  return android_os_version;
}

const char* GetLibcPath()
{	
  if (GetOSVersion() >= 10) {
    return LIBC_PATH_NEW;
  } else {
    return LIBC_PATH_OLD;
  }
}

const char* GetLinkerPath()
{
  if (GetOSVersion() >= 10) {
    return LINKER_PATH_NEW;
  } else {
    return LINKER_PATH_OLD;
  }
}

long CallMmap(pid_t pid, size_t length) {
  long function_addr = GetRemoteFuctionAddr(pid, GetLibcPath(), ((long) (void*)mmap));
  long params[6];
  params[0] = 0;
  params[1] = length;
  params[2] = PROT_READ | PROT_WRITE;
  params[3] = MAP_PRIVATE | MAP_ANONYMOUS;
  params[4] = 0;
  params[5] = 0;
  if (DEBUG) {
    printf("mmap called, function address %lx process %d size %zu\n", function_addr, pid, length);
  }
  return CallRemoteFunction(pid, function_addr, params, 6);
}

long CallMunmap(pid_t pid, long addr, size_t length) {
  long function_addr = GetRemoteFuctionAddr(pid, GetLibcPath(), ((long) (void*)munmap));
  long params[2];
  params[0] = addr;
  params[1] = length;
  if (DEBUG) {
    printf("munmap called, function address %lx process %d address %lx size %zu\n", function_addr, pid, addr, length);
  }
  return CallRemoteFunction(pid, function_addr, params, 2);
}

long CallDlopen(pid_t pid, const char* library_path) {
  long function_addr = GetRemoteFuctionAddr(pid, GetLinkerPath(), ((long) (void*)dlopen));
  long mmap_ret = CallMmap(pid, 0x400);
  PtraceWrite(pid, (uint8_t*)mmap_ret, (uint8_t*)library_path, strlen(library_path) + 1);
  long params[2];
  params[0] = mmap_ret;
  params[1] = RTLD_NOW | RTLD_LOCAL;
  if (DEBUG) {
    printf("dlopen called, function address %lx process %d library path %s\n", function_addr, pid, library_path);
  }
  long vndk_return_addr = GetModuleBaseAddr(pid, VNDK_LIB_PATH);
  long ret = CallRemoteFunctionFromNamespace(pid, function_addr, vndk_return_addr, params, 2);
  CallMunmap(pid, mmap_ret, 0x400);
  return ret;
}

long CallDlsym(pid_t pid, long so_handle, const char* symbol) {
  long function_addr = GetRemoteFuctionAddr(pid, GetLinkerPath(), ((long) (void*)dlsym));
  long mmap_ret = CallMmap(pid, 0x400);
  PtraceWrite(pid, (uint8_t*)mmap_ret, (uint8_t*)symbol, strlen(symbol) + 1);
  long params[2];
  params[0] = so_handle;
  params[1] = mmap_ret;
  if (DEBUG) {
    printf("dlsym called, function address %lx process %d so handle %lx symbol name %s\n", function_addr, pid, so_handle, symbol);
  }
  long ret = CallRemoteFunction(pid, function_addr, params, 2);
  CallMunmap(pid, mmap_ret, 0x400);
  return ret;
}

long CallDlclose(pid_t pid, long so_handle) {
  long function_addr = GetRemoteFuctionAddr(pid, GetLinkerPath(), ((long) (void*)dlclose));
  long params[1];
  params[0] = so_handle;
  if (DEBUG) {
    printf("dlclose called, function address %lx process %d so handle %lx\n", function_addr, pid, so_handle);
  }
  return CallRemoteFunction(pid, function_addr, params, 1);
}

long InjectLibrary(pid_t pid, const char* library_path) {
  if (DEBUG) {
    printf("Injection started...\n");
  }
  PtraceAttach(pid);
  long so_handle = CallDlopen(pid, library_path);
  if (DEBUG) {
    if (!so_handle) {
      printf("Injection failed...\n");
    } else {
      printf("Injection ended succesfully...\n");
    }
  }

  PtraceDetach(pid);
  return so_handle;
}
