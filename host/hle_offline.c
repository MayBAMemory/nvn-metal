// Kirby offline service draft (K08). Original policies; no third-party code copied.
// ABI: narrow bool/enum arguments use int/unsigned; no native C varargs.
// Shared D2R registrations are excluded, including SKF/SSF/SSC macros.
// Service results match pinned Ryujinx 1.3.2 or existing D2R policy.
#include "hle.h"

enum { K_BCAT_NOT_FOUND=(2u<<9)|122u, K_BCAT_INTERNET_DENIED=(8u<<9)|122u, K_NFP_NO_DEVICE=(64u<<9)|115u };

// _ZN2nn3err9ShowErrorENS_6ResultE
// Evidence: dm/host/hle_misc.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn3nfp10GetTagInfoEPNS0_7TagInfoERKNS0_12DeviceHandleE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/ResultCode.cs / Types/TagInfo.cs Size=0x58
// Policy: 清零TagInfo，DeviceNotFound=0x8073
HLE_FORCE("_ZN2nn3nfp10GetTagInfoEPNS0_7TagInfoERKNS0_12DeviceHandleE", "Kirby offline service boundary", Result, k_offline_001, void *out, const void *handle) { (void)handle; if(out)memset(out,0,0x58); return K_NFP_NO_DEVICE; }

// _ZN2nn3nfp10InitializeEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/ResultCode.cs
// Policy: 初始化成功，不建nfp会话；后续报告NFC不可用
HLE_FORCE("_ZN2nn3nfp10InitializeEv", "Kirby offline service boundary", Result, k_offline_002, void) { return RES_OK; }

// _ZN2nn3nfp11ListDevicesEPNS0_12DeviceHandleEPii
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/NfpManager/INfp.cs
// Policy: 模拟NFC关闭：成功且0个设备，未写设备数组；不用假设备句柄
HLE_FORCE("_ZN2nn3nfp11ListDevicesEPNS0_12DeviceHandleEPii", "Kirby offline service boundary", Result, k_offline_003, void *devices, int *count, int capacity) { (void)devices; (void)capacity; if(count)*count=0; return RES_OK; }

// _ZN2nn3nfp12GetModelInfoEPNS0_9ModelInfoERKNS0_12DeviceHandleE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/ResultCode.cs / Types/ModelInfo.cs Size=0x40
// Policy: 清零ModelInfo，DeviceNotFound
HLE_FORCE("_ZN2nn3nfp12GetModelInfoEPNS0_9ModelInfoERKNS0_12DeviceHandleE", "Kirby offline service boundary", Result, k_offline_004, void *out, const void *handle) { (void)handle; if(out)memset(out,0,0x40); return K_NFP_NO_DEVICE; }

// _ZN2nn3nfp13StopDetectionERKNS0_12DeviceHandleE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/ResultCode.cs
// Policy: Start/StopDetection：无设备；DeviceNotFound，不虚构标签
HLE_FORCE("_ZN2nn3nfp13StopDetectionERKNS0_12DeviceHandleE", "Kirby offline service boundary", Result, k_offline_005, const void *handle) { (void)handle; return K_NFP_NO_DEVICE; }

// _ZN2nn3nfp14GetDeviceStateERKNS0_12DeviceHandleE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/NfpManager/Types/NfpDeviceState.cs
// Policy: Unavailable=5
HLE_FORCE("_ZN2nn3nfp14GetDeviceStateERKNS0_12DeviceHandleE", "Kirby offline service boundary", unsigned, k_offline_006, const void *handle) { (void)handle; return 5; }

// _ZN2nn3nfp14StartDetectionERKNS0_12DeviceHandleE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/ResultCode.cs
// Policy: Start/StopDetection：无设备；DeviceNotFound，不虚构标签
HLE_FORCE("_ZN2nn3nfp14StartDetectionERKNS0_12DeviceHandleE", "Kirby offline service boundary", Result, k_offline_007, const void *handle) { (void)handle; return K_NFP_NO_DEVICE; }

// _ZN2nn3nfp29AttachAvailabilityChangeEventEPNS_2os15SystemEventTypeE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/NfpManager/INfp.cs / dm/host/hle_thread.c
// Policy: 真实宿主内核事件，永不触发；不用假句柄
HLE_FORCE("_ZN2nn3nfp29AttachAvailabilityChangeEventEPNS_2os15SystemEventTypeE", "Kirby offline service boundary", Result, k_offline_008, void *event) { hle_bind_system_event(event, 0); return RES_OK; }

// _ZN2nn3nfp5MountERKNS0_12DeviceHandleENS0_9ModelTypeENS0_11MountTargetE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/ResultCode.cs
// Policy: 无设备，不挂载标签，不写任何amiibo内容
HLE_FORCE("_ZN2nn3nfp5MountERKNS0_12DeviceHandleENS0_9ModelTypeENS0_11MountTargetE", "Kirby offline service boundary", Result, k_offline_009, const void *handle, int model, int target) { (void)handle; (void)model; (void)target; return K_NFP_NO_DEVICE; }

// _ZN2nn3nfp8FinalizeEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/ResultCode.cs
// Policy: 无会话可关闭
HLE_FORCE("_ZN2nn3nfp8FinalizeEv", "Kirby offline service boundary", void, k_offline_010, void) {  }

// _ZN2nn3nfp9GetNpadIdEPjRKNS0_12DeviceHandleE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/ResultCode.cs
// Policy: *out=0；无有效handle，不声称有手柄设备
HLE_FORCE("_ZN2nn3nfp9GetNpadIdEPjRKNS0_12DeviceHandleE", "Kirby offline service boundary", Result, k_offline_011, unsigned *out, const void *handle) { (void)handle; if(out)*out=0; return K_NFP_NO_DEVICE; }

// _ZN2nn3nsd9ResolveExEPNS0_4FqdnERKS1_
// Evidence: dm/host/hle_misc.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn3pdm30QueryApplicationPlayStatisticsEPNS0_25ApplicationPlayStatisticsERKNS_7account3UidEPKNS_13ApplicationIdEi
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Sdb/Pdm/QueryService/Types/ApplicationPlayStatistics.cs / SDK IPC数组stride=3*8
// Policy: 成功查询空统计；每项保留ApplicationId、总游玩时间=0、启动次数=0
HLE_FORCE("_ZN2nn3pdm30QueryApplicationPlayStatisticsEPNS0_25ApplicationPlayStatisticsERKNS_7account3UidEPKNS_13ApplicationIdEi", "Kirby offline service boundary", Result, k_offline_013, void *out, const void *uid, const unsigned long *ids, int count) { (void)uid; if(out && ids && count>0) { for(int i=0;i<count;i++) { unsigned long *row=(unsigned long *)((unsigned char *)out+(unsigned long)i*0x18); row[0]=ids[i]; row[1]=0; row[2]=0; } } return RES_OK; }

// _ZN2nn3ssl7Context15ImportServerPkiEPmPKcjNS0_17CertificateFormatE
// Evidence: dm/host/hle_net.c Context失败策略 / https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.HLE/HOS/Services/Ssl/ResultCode.cs
// Policy: *证书ID=0，未导入系统PKI，无假句柄
HLE_FORCE("_ZN2nn3ssl7Context15ImportServerPkiEPmPKcjNS0_17CertificateFormatE", "Kirby offline service boundary", Result, k_offline_014, void *self, unsigned long *id, const char *certificate, unsigned size, int format) { (void)self; (void)certificate; (void)size; (void)format; if(id)*id=0; return RES_FAIL; }

// _ZN2nn3ssl7Context6CreateENS1_10SslVersionE
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn3web19ShowOfflineHtmlPageEPNS0_26OfflineHtmlPageReturnValueERKNS0_22ShowOfflineHtmlPageArgE
// Evidence: dm/host/hle_misc.c 系统小程序不可用策略；SDK已构造返回对象
// Policy: 系统浏览器功能不可用；不打开小程序；不猜测私有对象大小
HLE_FORCE("_ZN2nn3web19ShowOfflineHtmlPageEPNS0_26OfflineHtmlPageReturnValueERKNS0_22ShowOfflineHtmlPageArgE", "Kirby offline service boundary", Result, k_offline_016, void *out, const void *arg) { (void)out; (void)arg; return RES_FAIL; }

// _ZN2nn3web22ShowOfflineHtmlPageArg17SetPointerEnabledEb
// Evidence: SDK参数/TLV builder和返回对象缓存；不连接系统小程序
// Policy: 保留SDK构造、setter与getter，避免伪造结构布局；只在Show边界失败
// No new registration: sdk

// _ZN2nn3web22ShowOfflineHtmlPageArg18SetFooterFixedKindENS0_22OfflineFooterFixedKindE
// Evidence: SDK参数/TLV builder和返回对象缓存；不连接系统小程序
// Policy: 保留SDK构造、setter与getter，避免伪造结构布局；只在Show边界失败
// No new registration: sdk

// _ZN2nn3web22ShowOfflineHtmlPageArg21SetJsExtensionEnabledEb
// Evidence: SDK参数/TLV builder和返回对象缓存；不连接系统小程序
// Policy: 保留SDK构造、setter与getter，避免伪造结构布局；只在Show边界失败
// No new registration: sdk

// _ZN2nn3web22ShowOfflineHtmlPageArg25SetTouchEnabledOnContentsEb
// Evidence: SDK参数/TLV builder和返回对象缓存；不连接系统小程序
// Policy: 保留SDK构造、setter与getter，避免伪造结构布局；只在Show边界失败
// No new registration: sdk

// _ZN2nn3web22ShowOfflineHtmlPageArgC1EPKc
// Evidence: SDK参数/TLV builder和返回对象缓存；不连接系统小程序
// Policy: 保留SDK构造、setter与getter，避免伪造结构布局；只在Show边界失败
// No new registration: sdk

// _ZN2nn3web26OfflineHtmlPageReturnValueC1Ev
// Evidence: SDK参数/TLV builder和返回对象缓存；不连接系统小程序
// Policy: 保留SDK构造、setter与getter，避免伪造结构布局；只在Show边界失败
// No new registration: sdk

// _ZN2nn4bcat10InitializeEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: 无IPC初始化；不把网络失败当启动错误
HLE_FORCE("_ZN2nn4bcat10InitializeEv", "Kirby offline service boundary", void, k_offline_023, void) {  }

// _ZN2nn4bcat17DeliveryCacheFile4OpenEPKc
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: NotFound=0x47a；不创建文件/目录会话
HLE_FORCE("_ZN2nn4bcat17DeliveryCacheFile4OpenEPKc", "Kirby offline service boundary", Result, k_offline_024, void *self, const char *name) { (void)self; (void)name; return K_BCAT_NOT_FOUND; }

// _ZN2nn4bcat17DeliveryCacheFile4ReadEPmlPvm
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: *读取字节=0；不写buffer；NotFound
HLE_FORCE("_ZN2nn4bcat17DeliveryCacheFile4ReadEPmlPvm", "Kirby offline service boundary", Result, k_offline_025, void *self, unsigned long *count, long offset, void *buffer, unsigned long size) { (void)self; (void)offset; (void)buffer; (void)size; if(count)*count=0; return K_BCAT_NOT_FOUND; }

// _ZN2nn4bcat17DeliveryCacheFile5CloseEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: 无打开会话，安全空关闭
HLE_FORCE("_ZN2nn4bcat17DeliveryCacheFile5CloseEv", "Kirby offline service boundary", void, k_offline_026, void *self) { (void)self; }

// _ZN2nn4bcat17DeliveryCacheFileC1Ev
// Evidence: sdk构造/析构：File/Directory C1为str xzr,[x0]；未打开时D1直接ret；Progress构造本地EventType（sdk+0x7fee8）
// Policy: 保留SDK对象生命周期；服务Open始终失败所以没有服务会话可析构
// No new registration: sdk

// _ZN2nn4bcat17DeliveryCacheFileD1Ev
// Evidence: sdk构造/析构：File/Directory C1为str xzr,[x0]；未打开时D1直接ret；Progress构造本地EventType（sdk+0x7fee8）
// Policy: 保留SDK对象生命周期；服务Open始终失败所以没有服务会话可析构
// No new registration: sdk

// _ZN2nn4bcat21DeliveryCacheProgress6UpdateEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/Ipc/ServiceCreator/Types/DeliveryCacheProgressImpl.cs
// Policy: 不拉取服务数据；GetStatus固定Done=9
HLE_FORCE("_ZN2nn4bcat21DeliveryCacheProgress6UpdateEv", "Kirby offline service boundary", void, k_offline_029, void *self) { (void)self; }

// _ZN2nn4bcat21DeliveryCacheProgressC1Ev
// Evidence: sdk构造/析构：File/Directory C1为str xzr,[x0]；未打开时D1直接ret；Progress构造本地EventType（sdk+0x7fee8）
// Policy: 保留SDK对象生命周期；服务Open始终失败所以没有服务会话可析构
// No new registration: sdk

// _ZN2nn4bcat21DeliveryCacheProgressD1Ev
// Evidence: sdk构造/析构：File/Directory C1为str xzr,[x0]；未打开时D1直接ret；Progress构造本地EventType（sdk+0x7fee8）
// Policy: 保留SDK对象生命周期；服务Open始终失败所以没有服务会话可析构
// No new registration: sdk

// _ZN2nn4bcat22DeliveryCacheDirectory4OpenEPKc
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: NotFound=0x47a；不创建文件/目录会话
HLE_FORCE("_ZN2nn4bcat22DeliveryCacheDirectory4OpenEPKc", "Kirby offline service boundary", Result, k_offline_032, void *self, const char *name) { (void)self; (void)name; return K_BCAT_NOT_FOUND; }

// _ZN2nn4bcat22DeliveryCacheDirectory4ReadEPiPNS0_27DeliveryCacheDirectoryEntryEi
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: *条目数=0；不写entries；NotFound
HLE_FORCE("_ZN2nn4bcat22DeliveryCacheDirectory4ReadEPiPNS0_27DeliveryCacheDirectoryEntryEi", "Kirby offline service boundary", Result, k_offline_033, void *self, int *count, void *entries, int capacity) { (void)self; (void)entries; (void)capacity; if(count)*count=0; return K_BCAT_NOT_FOUND; }

// _ZN2nn4bcat22DeliveryCacheDirectory5CloseEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: 无打开会话，安全空关闭
HLE_FORCE("_ZN2nn4bcat22DeliveryCacheDirectory5CloseEv", "Kirby offline service boundary", void, k_offline_034, void *self) { (void)self; }

// _ZN2nn4bcat22DeliveryCacheDirectoryC1Ev
// Evidence: sdk构造/析构：File/Directory C1为str xzr,[x0]；未打开时D1直接ret；Progress构造本地EventType（sdk+0x7fee8）
// Policy: 保留SDK对象生命周期；服务Open始终失败所以没有服务会话可析构
// No new registration: sdk

// _ZN2nn4bcat22DeliveryCacheDirectoryD1Ev
// Evidence: sdk构造/析构：File/Directory C1为str xzr,[x0]；未打开时D1直接ret；Progress构造本地EventType（sdk+0x7fee8）
// Policy: 保留SDK对象生命周期；服务Open始终失败所以没有服务会话可析构
// No new registration: sdk

// _ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/Ipc/ServiceCreator/BcatService.cs
// Policy: 接受异步请求，进度立即终止失败；不创建服务句柄
HLE_FORCE("_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressE", "Kirby offline service boundary", Result, k_offline_037, void *progress) { (void)progress; return RES_OK; }

// _ZN2nn4bcat25MountDeliveryCacheStorageEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: 虚拟空缓存；不挂系统存档；卸载为空操作
HLE_FORCE("_ZN2nn4bcat25MountDeliveryCacheStorageEv", "Kirby offline service boundary", Result, k_offline_038, void) { return RES_OK; }

// _ZN2nn4bcat27UnmountDeliveryCacheStorageEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: 虚拟空缓存；不挂系统存档；卸载为空操作
HLE_FORCE("_ZN2nn4bcat27UnmountDeliveryCacheStorageEv", "Kirby offline service boundary", void, k_offline_039, void) {  }

// _ZN2nn4bcat31EnumerateDeliveryCacheDirectoryEPiPNS0_13DirectoryNameEi
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: 成功返回0个目录，数组不使用
HLE_FORCE("_ZN2nn4bcat31EnumerateDeliveryCacheDirectoryEPiPNS0_13DirectoryNameEi", "Kirby offline service boundary", Result, k_offline_040, int *count, void *names, int capacity) { (void)names; (void)capacity; if (count) *count=0; return RES_OK; }

// _ZN2nn4nifm10InitializeEv
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn4nifm18IsNetworkAvailableEv
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn4nifm20CancelNetworkRequestEv
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn4nifm20SubmitNetworkRequestEv
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn4nifm22GetCurrentProxySettingEPNS0_12ProxySettingE
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn4nifm22IsNetworkRequestOnHoldEv
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn4nifm26GetCurrentPrimaryIpAddressEP7in_addr
// Evidence: dm/host/hle_net.c GetCurrentPrimaryIpAddress（原签名的类型名不同）
// Policy: 地址=0；网络不可用，失败Result
HLE_FORCE("_ZN2nn4nifm26GetCurrentPrimaryIpAddressEP7in_addr", "Kirby offline service boundary", Result, k_offline_047, unsigned *address) { if(address)*address=0; return RES_FAIL; }

// _ZN2nn4nifm31HandleNetworkRequestErrorResultEv
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn5prepo10InitializeEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs
// Policy: 不建上报会话；本地报告构造仍使用SDK
HLE_FORCE("_ZN2nn5prepo10InitializeEv", "Kirby offline service boundary", void, k_offline_049, void) {  }

// _ZN2nn5prepo10PlayReport10SetEventIdEPKc
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo10PlayReport3AddEPKcRKNS0_5ArrayE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo10PlayReport3AddEPKcS3_
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo10PlayReport3AddEPKcb
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo10PlayReport3AddEPKcd
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo10PlayReport3AddEPKcl
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo10PlayReport4SaveERKNS_7account3UidE
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs
// Policy: 成功丢弃报告；无网络/遥测发送；避免把可选统计当致命错误
HLE_FORCE("_ZN2nn5prepo10PlayReport4SaveERKNS_7account3UidE", "Kirby offline service boundary", Result, k_offline_056, void *self, const void *uid) { (void)self; (void)uid; return RES_OK; }

// _ZN2nn5prepo10PlayReport9SetBufferEPvm
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo10PlayReportC1Ev
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo5Array3AddEl
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo5Array9SetBufferEPvm
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn5prepo5ArrayC1Ev
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// _ZN2nn6socket10GetSockOptEiiiPvPj
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket10GetSockOptEiiiPvPj", "Kirby offline service boundary", int, k_offline_062, int fd, int level, int option, void *value, unsigned *size) { (void)fd; (void)level; (void)option; (void)value; (void)size; return -1; }

// _ZN2nn6socket10InitializeERKNS0_6ConfigE
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn6socket10SetSockOptEiiiPKvj
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket10SetSockOptEiiiPKvj", "Kirby offline service boundary", int, k_offline_064, int fd, int level, int option, const void *value, unsigned size) { (void)fd; (void)level; (void)option; (void)value; (void)size; return -1; }

// _ZN2nn6socket11GetAddrInfoEPKcS2_PK8addrinfoPPS3_
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: EAI_FAIL=4，*out=NULL
HLE_FORCE("_ZN2nn6socket11GetAddrInfoEPKcS2_PK8addrinfoPPS3_", "Kirby offline service boundary", int, k_offline_065, const char *host, const char *service, const void *hint, void **out) { (void)host; (void)service; (void)hint; if (out) *out = NULL; return 4; }

// _ZN2nn6socket11GetSockNameEiP8sockaddrPj
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket11GetSockNameEiP8sockaddrPj", "Kirby offline service boundary", int, k_offline_066, int fd, void *addr, unsigned *size) { (void)fd; (void)addr; (void)size; return -1; }

// _ZN2nn6socket12FreeAddrInfoEP8addrinfo
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: 请求失败从不分配addrinfo；安全空释放
HLE_FORCE("_ZN2nn6socket12FreeAddrInfoEP8addrinfo", "Kirby offline service boundary", void, k_offline_067, void *info) { (void)info; }

// _ZN2nn6socket12GetLastErrnoEv
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: Horizon/FreeBSD ENETDOWN=50；不用Windows errno
HLE_FORCE("_ZN2nn6socket12GetLastErrnoEv", "Kirby offline service boundary", int, k_offline_068, void) { return 50; }

// _ZN2nn6socket4BindEiPK8sockaddrj
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket4BindEiPK8sockaddrj", "Kirby offline service boundary", int, k_offline_069, int fd, const void *addr, unsigned size) { (void)fd; (void)addr; (void)size; return -1; }

// _ZN2nn6socket4PollEP6pollfdmi
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket4PollEP6pollfdmi", "Kirby offline service boundary", int, k_offline_070, void *fds, unsigned long count, int timeout) { (void)fds; (void)count; (void)timeout; return -1; }

// _ZN2nn6socket4RecvEiPvmi
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket4RecvEiPvmi", "Kirby offline service boundary", long, k_offline_071, int fd, void *buffer, unsigned long size, int flags) { (void)fd; (void)buffer; (void)size; (void)flags; return -1; }

// _ZN2nn6socket4SendEiPKvmi
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket4SendEiPKvmi", "Kirby offline service boundary", long, k_offline_072, int fd, const void *buffer, unsigned long size, int flags) { (void)fd; (void)buffer; (void)size; (void)flags; return -1; }

// _ZN2nn6socket5CloseEi
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn6socket5FcntlEiiz
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket5FcntlEiiz", "Kirby offline service boundary", int, k_offline_074, int fd, int command) { (void)fd; (void)command; return -1; }

// _ZN2nn6socket6AcceptEiP8sockaddrPj
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket6AcceptEiP8sockaddrPj", "Kirby offline service boundary", int, k_offline_075, int fd, void *addr, unsigned *size) { (void)fd; (void)addr; (void)size; return -1; }

// _ZN2nn6socket6ListenEii
// Evidence: dm/host/hle_net.c（精确改编名已登记，含宏展开项）
// Policy: 复用D2R公共接管；不重复登记
// No new registration: reuse

// _ZN2nn6socket6SendToEiPKvmiPK8sockaddrj
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket6SendToEiPKvmiPK8sockaddrj", "Kirby offline service boundary", long, k_offline_077, int fd, const void *buffer, unsigned long size, int flags, const void *addr, unsigned addr_size) { (void)fd; (void)buffer; (void)size; (void)flags; (void)addr; (void)addr_size; return -1; }

// _ZN2nn6socket6SocketEiii
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket6SocketEiii", "Kirby offline service boundary", int, k_offline_078, int family, int type, int protocol) { (void)family; (void)type; (void)protocol; return -1; }

// _ZN2nn6socket7ConnectEiPK8sockaddrj
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket7ConnectEiPK8sockaddrj", "Kirby offline service boundary", int, k_offline_079, int fd, const void *addr, unsigned size) { (void)fd; (void)addr; (void)size; return -1; }

// _ZN2nn6socket8FinalizeEv
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: 无会话，无操作
HLE_FORCE("_ZN2nn6socket8FinalizeEv", "Kirby offline service boundary", void, k_offline_080, void) {  }

// _ZN2nn6socket8InetNtoaE7in_addr
// Evidence: SDK地址与字节序纯计算；dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: 保留SDK原实现，非服务调用
// No new registration: sdk

// _ZN2nn6socket8InetNtopEiPKvPcj
// Evidence: SDK地址与字节序纯计算；dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: 保留SDK原实现，非服务调用
// No new registration: sdk

// _ZN2nn6socket8InetPtonEiPKcPv
// Evidence: SDK地址与字节序纯计算；dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: 保留SDK原实现，非服务调用
// No new registration: sdk

// _ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket8RecvFromEiPvmiP8sockaddrPj", "Kirby offline service boundary", long, k_offline_084, int fd, void *buffer, unsigned long size, int flags, void *addr, unsigned *addr_size) { (void)fd; (void)buffer; (void)size; (void)flags; (void)addr; (void)addr_size; return -1; }

// _ZN2nn6socket8ShutdownEii
// Evidence: dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: -1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数
HLE_FORCE("_ZN2nn6socket8ShutdownEii", "Kirby offline service boundary", int, k_offline_085, int fd, int how) { (void)fd; (void)how; return -1; }

// _ZN2nn6socket9InetHtonlEj
// Evidence: SDK地址与字节序纯计算；dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: 保留SDK原实现，非服务调用
// No new registration: sdk

// _ZN2nn6socket9InetHtonsEt
// Evidence: SDK地址与字节序纯计算；dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: 保留SDK原实现，非服务调用
// No new registration: sdk

// _ZN2nn6socket9InetNtohlEj
// Evidence: SDK地址与字节序纯计算；dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: 保留SDK原实现，非服务调用
// No new registration: sdk

// _ZN2nn6socket9InetNtohsEt
// Evidence: SDK地址与字节序纯计算；dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract
// Policy: 保留SDK原实现，非服务调用
// No new registration: sdk

// _ZNK2nn3web26OfflineHtmlPageReturnValue10GetLastUrlEv
// Evidence: SDK参数/TLV builder和返回对象缓存；不连接系统小程序
// Policy: 保留SDK构造、setter与getter，避免伪造结构布局；只在Show边界失败
// No new registration: sdk

// _ZNK2nn3web26OfflineHtmlPageReturnValue20GetOfflineExitReasonEv
// Evidence: SDK参数/TLV builder和返回对象缓存；不连接系统小程序
// Policy: 保留SDK构造、setter与getter，避免伪造结构布局；只在Show边界失败
// No new registration: sdk

// _ZNK2nn4bcat21DeliveryCacheProgress9GetResultEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/BcatResult.cs
// Policy: module122/description8，InternetRequestDenied=0x107a
HLE_FORCE("_ZNK2nn4bcat21DeliveryCacheProgress9GetResultEv", "Kirby offline service boundary", Result, k_offline_092, const void *self) { (void)self; return K_BCAT_INTERNET_DENIED; }

// _ZNK2nn4bcat21DeliveryCacheProgress9GetStatusEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Bcat/Ipc/ServiceCreator/Types/DeliveryCacheProgressImpl.cs
// Policy: Done=9，避免轮询卡死
HLE_FORCE("_ZNK2nn4bcat21DeliveryCacheProgress9GetStatusEv", "Kirby offline service boundary", unsigned, k_offline_093, const void *self) { (void)self; return 9; }

// _ZNK2nn5prepo10PlayReport8GetCountEv
// Evidence: https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs / SDK PlayReport、Array本地msgpack builder
// Policy: 保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃
// No new registration: sdk

// glslcCompile
// Evidence: TASKS.md K08 explicit compiler-failure policy
// Policy: w0=0；忽略全部参数；不写输出；不启动编译
HLE("glslcCompile", int, k_offline_095, void) { return 0; }

// glslcCompilePreSpecialized
// Evidence: TASKS.md K08 explicit compiler-failure policy
// Policy: w0=0；忽略全部参数；不写输出；不启动编译
HLE("glslcCompilePreSpecialized", int, k_offline_096, void) { return 0; }

// glslcCompileSpecialized
// Evidence: TASKS.md K08 explicit compiler-failure policy
// Policy: w0=0；忽略全部参数；不写输出；不启动编译
HLE("glslcCompileSpecialized", int, k_offline_097, void) { return 0; }

// glslcFinalize
// Evidence: TASKS.md K08 explicit compiler-failure policy
// Policy: w0=0；忽略全部参数；不写输出；不启动编译
HLE("glslcFinalize", int, k_offline_098, void) { return 0; }

// glslcGetDefaultOptions
// Evidence: TASKS.md K08 explicit compiler-failure policy
// Policy: w0=0；忽略全部参数；不写输出；不启动编译
HLE("glslcGetDefaultOptions", int, k_offline_099, void) { return 0; }

// glslcGetVersion
// Evidence: TASKS.md K08 explicit compiler-failure policy
// Policy: w0=0；忽略全部参数；不写输出；不启动编译
HLE("glslcGetVersion", int, k_offline_100, void) { return 0; }

// glslcInitialize
// Evidence: TASKS.md K08 explicit compiler-failure policy
// Policy: w0=0；忽略全部参数；不写输出；不启动编译
HLE("glslcInitialize", int, k_offline_101, void) { return 0; }

// glslcSetAllocator
// Evidence: TASKS.md K08 explicit compiler-failure policy
// Policy: w0=0；忽略全部参数；不写输出；不启动编译
HLE("glslcSetAllocator", int, k_offline_102, void) { return 0; }
