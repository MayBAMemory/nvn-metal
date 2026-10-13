"""Generate K08's import-complete offline policy and nonduplicate C draft.
Usage: python -I offline_inventory.py --repo <repo> --refs <reloc_refs.txt>
Policies reference pinned Ryujinx 1.3.2 and existing D2R contracts. Pure SDK
builders/getters are retained; only service boundaries receive interposers.
"""
import argparse
import csv
import re
import sys
from collections import Counter
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from imports_compare import strip_comments

NAMESPACES = {'nn::bcat', 'nn::prepo', 'nn::nfp', 'nn::web', 'nn::ssl', 'nn::socket',
              'nn::nifm', 'nn::nsd', 'nn::err', 'nn::pdm'}
SHARED = ('hle_net.c', 'hle_friends.c', 'hle_misc.c', 'hle_account.c')
UPSTREAM = 'https://github.com/Leuconoe/Ryubing/blob/9d83dfd19cd838042abb833891da6aa9dee8b2bc/'


def policy(row):
    ns, name, symbol = row['ns'], row['demangled'], row['mangled']
    function = name.split('(', 1)[0].split('::')[-1]
    # Return: strategy, C return type, argument list, body, evidence, behaviour.
    if symbol.startswith('glslc'):
        return 'stub', 'int', 'void', 'return 0;', 'TASKS.md K08 explicit compiler-failure policy', 'w0=0；忽略全部参数；不写输出；不启动编译'
    if ns == 'nn::socket':
        ref = 'dm/host/hle_net.c BSD ENETDOWN=50 / EAI_FAIL=4 contract'
        if function.startswith('Inet'): return 'sdk', '', '', '', 'SDK地址与字节序纯计算；'+ref, '保留SDK原实现，非服务调用'
        if function == 'Initialize': return 'stub', 'Result', 'const void *config', '(void)config; return RES_OK;', ref, '初始化成功，不建bsd会话'
        if function == 'Finalize': return 'stub', 'void', 'void', '', ref, '无会话，无操作'
        if function == 'GetLastErrno': return 'stub', 'int', 'void', 'return 50;', ref, 'Horizon/FreeBSD ENETDOWN=50；不用Windows errno'
        if function == 'GetAddrInfo':
            return 'stub', 'int', 'const char *host, const char *service, const void *hint, void **out', '(void)host; (void)service; (void)hint; if (out) *out = NULL; return 4;', ref, 'EAI_FAIL=4，*out=NULL'
        if function == 'FreeAddrInfo': return 'stub', 'void', 'void *info', '(void)info;', ref, '请求失败从不分配addrinfo；安全空释放'
        arguments = {
            'Socket': 'int family, int type, int protocol', 'Bind': 'int fd, const void *addr, unsigned size',
            'Connect': 'int fd, const void *addr, unsigned size', 'Listen': 'int fd, int backlog',
            'Accept': 'int fd, void *addr, unsigned *size', 'Close': 'int fd', 'Shutdown': 'int fd, int how',
            'GetSockName': 'int fd, void *addr, unsigned *size', 'GetSockOpt': 'int fd, int level, int option, void *value, unsigned *size',
            'SetSockOpt': 'int fd, int level, int option, const void *value, unsigned size',
            'Fcntl': 'int fd, int command', 'Poll': 'void *fds, unsigned long count, int timeout',
            'Recv': 'int fd, void *buffer, unsigned long size, int flags',
            'Send': 'int fd, const void *buffer, unsigned long size, int flags',
            'RecvFrom': 'int fd, void *buffer, unsigned long size, int flags, void *addr, unsigned *addr_size',
            'SendTo': 'int fd, const void *buffer, unsigned long size, int flags, const void *addr, unsigned addr_size'}
        if function not in arguments: raise ValueError('Unknown socket function: '+name)
        args = arguments[function]
        ignored = ['(void)'+arg.split()[-1].lstrip('*')+';' for arg in args.split(', ')]
        body = ' '.join(ignored) + ' return -1;'
        ret = 'long' if function in ('Send','Recv','SendTo','RecvFrom') else 'int'
        return 'stub', ret, args, body, ref, '-1；不访问网络、不修改接收/地址缓冲；GetLastErrno固定50；Fcntl忽略寄存器可变参数'
    if ns == 'nn::bcat':
        ref = UPSTREAM+'src/Ryujinx.Horizon/Bcat/BcatResult.cs'
        if function == 'Initialize': return 'stub','void','void','',ref,'无IPC初始化；不把网络失败当启动错误'
        if function in ('MountDeliveryCacheStorage','UnmountDeliveryCacheStorage'):
            return 'stub','Result' if function.startswith('Mount') else 'void','void','return RES_OK;' if function.startswith('Mount') else '',ref,'虚拟空缓存；不挂系统存档；卸载为空操作'
        if function == 'EnumerateDeliveryCacheDirectory':
            return 'stub','Result','int *count, void *names, int capacity','(void)names; (void)capacity; if (count) *count=0; return RES_OK;',ref,'成功返回0个目录，数组不使用'
        if function == 'RequestSyncDeliveryCache':
            return 'stub','Result','void *progress','(void)progress; return RES_OK;',UPSTREAM+'src/Ryujinx.Horizon/Bcat/Ipc/ServiceCreator/BcatService.cs','接受异步请求，进度立即终止失败；不创建服务句柄'
        if 'DeliveryCacheProgress' in name and function in ('Update','GetStatus','GetResult'):
            ref2=UPSTREAM+'src/Ryujinx.Horizon/Bcat/Ipc/ServiceCreator/Types/DeliveryCacheProgressImpl.cs'
            if function=='Update':return 'stub','void','void *self','(void)self;',ref2,'不拉取服务数据；GetStatus固定Done=9'
            if function=='GetStatus':return 'stub','unsigned','const void *self','(void)self; return 9;',ref2,'Done=9，避免轮询卡死'
            return 'stub','Result','const void *self','(void)self; return K_BCAT_INTERNET_DENIED;',ref,'module122/description8，InternetRequestDenied=0x107a'
        if function=='Open':return 'stub','Result','void *self, const char *name','(void)self; (void)name; return K_BCAT_NOT_FOUND;',ref,'NotFound=0x47a；不创建文件/目录会话'
        if function=='Close':return 'stub','void','void *self','(void)self;',ref,'无打开会话，安全空关闭'
        if function=='Read':
            if 'DeliveryCacheFile' in name:
                return 'stub','Result','void *self, unsigned long *count, long offset, void *buffer, unsigned long size','(void)self; (void)offset; (void)buffer; (void)size; if(count)*count=0; return K_BCAT_NOT_FOUND;',ref,'*读取字节=0；不写buffer；NotFound'
            return 'stub','Result','void *self, int *count, void *entries, int capacity','(void)self; (void)entries; (void)capacity; if(count)*count=0; return K_BCAT_NOT_FOUND;',ref,'*条目数=0；不写entries；NotFound'
        return 'sdk','','','','sdk构造/析构：File/Directory C1为str xzr,[x0]；未打开时D1直接ret；Progress构造本地EventType（sdk+0x7fee8）','保留SDK对象生命周期；服务Open始终失败所以没有服务会话可析构'
    if ns=='nn::prepo':
        ref=UPSTREAM+'src/Ryujinx.Horizon/Prepo/Ipc/PrepoService.cs'
        if function=='Initialize':return 'stub','void','void','',ref,'不建上报会话；本地报告构造仍使用SDK'
        if function=='Save':return 'stub','Result','void *self, const void *uid','(void)self; (void)uid; return RES_OK;',ref,'成功丢弃报告；无网络/遥测发送；避免把可选统计当致命错误'
        return 'sdk','','','',ref+' / SDK PlayReport、Array本地msgpack builder','保留SDK构造、SetBuffer、Add、GetCount等本地序列化与缓存读写；保存服务边界丢弃'
    if ns=='nn::nfp':
        ref=UPSTREAM+'src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/ResultCode.cs'
        if function=='Initialize':return 'stub','Result','void','return RES_OK;',ref,'初始化成功，不建nfp会话；后续报告NFC不可用'
        if function=='Finalize':return 'stub','void','void','',ref,'无会话可关闭'
        if function=='ListDevices':return 'stub','Result','void *devices, int *count, int capacity','(void)devices; (void)capacity; if(count)*count=0; return RES_OK;',UPSTREAM+'src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/NfpManager/INfp.cs','模拟NFC关闭：成功且0个设备，未写设备数组；不用假设备句柄'
        if function=='GetDeviceState':return 'stub','unsigned','const void *handle','(void)handle; return 5;',UPSTREAM+'src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/NfpManager/Types/NfpDeviceState.cs','Unavailable=5'
        if function=='AttachAvailabilityChangeEvent':return 'stub','Result','void *event','hle_bind_system_event(event, 0); return RES_OK;',UPSTREAM+'src/Ryujinx.HLE/HOS/Services/Nfc/Nfp/NfpManager/INfp.cs / dm/host/hle_thread.c','真实宿主内核事件，永不触发；不用假句柄'
        if function=='GetTagInfo':return 'stub','Result','void *out, const void *handle','(void)handle; if(out)memset(out,0,0x58); return K_NFP_NO_DEVICE;',ref+' / Types/TagInfo.cs Size=0x58','清零TagInfo，DeviceNotFound=0x8073'
        if function=='GetModelInfo':return 'stub','Result','void *out, const void *handle','(void)handle; if(out)memset(out,0,0x40); return K_NFP_NO_DEVICE;',ref+' / Types/ModelInfo.cs Size=0x40','清零ModelInfo，DeviceNotFound'
        if function=='GetNpadId':return 'stub','Result','unsigned *out, const void *handle','(void)handle; if(out)*out=0; return K_NFP_NO_DEVICE;',ref,'*out=0；无有效handle，不声称有手柄设备'
        if function=='Mount':return 'stub','Result','const void *handle, int model, int target','(void)handle; (void)model; (void)target; return K_NFP_NO_DEVICE;',ref,'无设备，不挂载标签，不写任何amiibo内容'
        return 'stub','Result','const void *handle','(void)handle; return K_NFP_NO_DEVICE;',ref,'Start/StopDetection：无设备；DeviceNotFound，不虚构标签'
    if ns=='nn::web':
        if function=='ShowOfflineHtmlPage':return 'stub','Result','void *out, const void *arg','(void)out; (void)arg; return RES_FAIL;','dm/host/hle_misc.c 系统小程序不可用策略；SDK已构造返回对象','系统浏览器功能不可用；不打开小程序；不猜测私有对象大小'
        return 'sdk','','','','SDK参数/TLV builder和返回对象缓存；不连接系统小程序','保留SDK构造、setter与getter，避免伪造结构布局；只在Show边界失败'
    if ns=='nn::ssl':
        ref='dm/host/hle_net.c Context失败策略 / '+UPSTREAM+'src/Ryujinx.HLE/HOS/Services/Ssl/ResultCode.cs'
        if function=='Create':return 'stub','Result','void *self, int version','(void)self; (void)version; return RES_FAIL;',ref,'通用离线失败，与D2R Context策略相同；无会话'
        return 'stub','Result','void *self, unsigned long *id, const char *certificate, unsigned size, int format','(void)self; (void)certificate; (void)size; (void)format; if(id)*id=0; return RES_FAIL;',ref,'*证书ID=0，未导入系统PKI，无假句柄'
    if ns=='nn::pdm':
        return 'stub','Result','void *out, const void *uid, const unsigned long *ids, int count','(void)uid; if(out && ids && count>0) { for(int i=0;i<count;i++) { unsigned long *row=(unsigned long *)((unsigned char *)out+(unsigned long)i*0x18); row[0]=ids[i]; row[1]=0; row[2]=0; } } return RES_OK;',UPSTREAM+'src/Ryujinx.HLE/HOS/Services/Sdb/Pdm/QueryService/Types/ApplicationPlayStatistics.cs / SDK IPC数组stride=3*8','成功查询空统计；每项保留ApplicationId、总游玩时间=0、启动次数=0'
    if ns=='nn::nifm' and function=='GetCurrentPrimaryIpAddress':return 'stub','Result','unsigned *address','if(address)*address=0; return RES_FAIL;','dm/host/hle_net.c GetCurrentPrimaryIpAddress（原签名的类型名不同）','地址=0；网络不可用，失败Result'
    return 'reuse','','','','已有D2R公共接管','复用已有实现'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);ap.add_argument('--refs',type=Path,required=True)
    args=ap.parse_args();repo=args.repo
    imports=list(csv.DictReader((repo/'kirby/analysis/imports_main.tsv').open(encoding='utf-8'),delimiter='\t'))
    selected=[r for r in imports if r['ns'] in NAMESPACES or r['mangled'].startswith('glslc')]
    refs=set(args.refs.read_text(encoding='utf-8').splitlines())
    existing={}
    for file in SHARED:
        text=strip_comments((repo/'dm/host'/file).read_text(encoding='utf-8'))
        for name in re.findall(r'"((?:_Z|nnssl|nnsocket)[^"\n]+)"',text):existing[name]=file
    # Include direct registrations from K02 as requested; scan quoted mangled names
    # as well, because SKF/SSF/SSC helper macros are not expanded by that old table.
    for row in csv.DictReader((repo/'kirby/analysis/d2r_hle_in_kirby.tsv').open(encoding='utf-8'),delimiter='\t'):
        if Path(row['d2r_file']).name in SHARED:existing[row['name']]=Path(row['d2r_file']).name
    source=['// Kirby offline service draft (K08). Original policies; no third-party code copied.',
            '// ABI: narrow bool/enum arguments use int/unsigned; no native C varargs.',
            '// Shared D2R registrations are excluded, including SKF/SSF/SSC macros.',
            '// Service results match pinned Ryujinx 1.3.2 or existing D2R policy.',
            '#include "hle.h"', '',
            'enum { K_BCAT_NOT_FOUND=(2u<<9)|122u, K_BCAT_INTERNET_DENIED=(8u<<9)|122u, K_NFP_NO_DEVICE=(64u<<9)|115u };', '']
    rows=[];counts=Counter();registered=[]
    for n,row in enumerate(selected):
        strategy,ret,arguments,body,evidence,behaviour=policy(row)
        name=row['mangled'];cname='k_offline_%03d'%n
        if name in existing:
            strategy='reuse';evidence='dm/host/'+existing[name]+'（精确改编名已登记，含宏展开项）';behaviour='复用D2R公共接管；不重复登记'
        if strategy=='stub' and name not in refs: strategy='unreferenced';behaviour='没有实际重定位引用，不加入强制表；原建议：'+behaviour
        counts[strategy]+=1
        source += ['// '+name, '// Evidence: '+evidence, '// Policy: '+behaviour]
        if strategy=='stub':
            macro='HLE' if name.startswith('glslc') else 'HLE_FORCE'
            if macro=='HLE':line='HLE("%s", %s, %s, %s) { %s }'%(name,ret,cname,arguments,body)
            else:line='HLE_FORCE("%s", "Kirby offline service boundary", %s, %s, %s) { %s }'%(name,ret,cname,arguments,body)
            source += [line,''];registered.append(name)
        else:source += ['// No new registration: '+strategy,'']
        rows.append([name,row['demangled'],row['ns'],strategy,behaviour,evidence,int(name in refs)])
    docs=repo/'kirby/docs';host=repo/'kirby/host';docs.mkdir(exist_ok=True);host.mkdir(exist_ok=True)
    with (docs/'offline_stubs.tsv').open('w',encoding='utf-8',newline='') as f:
        writer=csv.writer(f,delimiter='\t',lineterminator='\n')
        writer.writerow(['mangled','demangled','namespace','strategy','behaviour','evidence','relocated']);writer.writerows(rows)
    (host/'hle_offline.c').write_text('\n'.join(source),encoding='utf-8')
    assert len(registered)==len(set(registered)) and not (set(registered)&set(existing))
    print('K08 imports',len(selected),'strategies',dict(counts),'new registrations',len(registered))


if __name__=='__main__':main()
