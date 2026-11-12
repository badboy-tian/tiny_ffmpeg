#include "cuva_packet.h"
#include <cassert>
#include <iostream>
using namespace cuva_packet;
Bytes annex(const std::vector<Bytes> &nals) {Bytes out;for(auto &n:nals){out.insert(out.end(),{0,0,0,1});out.insert(out.end(),n.begin(),n.end());}return out;}
int main(){
 Message cuva{4,{0x26,0,4,0,5,1,0,0,1,0,0,2,0,0,3,0,0,0}};
 Message unrelated{4,{0xb5,0,0x3c,0,1,4,0}};
 Message staticHdr{137,Bytes(280,7)};
 std::vector<Message> meta;
 auto sei=buildSei(78,1,{cuva,unrelated,staticHdr});
 assert(messages(sei,meta)&&meta.size()==3&&meta[0].payload==cuva.payload);
 auto packet=annex({sei,{0x26,1,0x11,0x80}});
 assert(extract(packet.data(),packet.size(),0,meta)&&meta.size()==1&&meta[0].payload==cuva.payload);
 // 不同长度前缀和emulation prevention往返必须保留原始T.35字节。
 for(int n:{1,2,3,4}){
  auto shortSei=buildSei(78,1,{cuva});Bytes length;
  for(int i=n-1;i>=0;i--)length.push_back(shortSei.size()>>(i*8));
  length.insert(length.end(),shortSei.begin(),shortSei.end());
  assert(extract(length.data(),length.size(),n,meta)&&meta.size()==1&&meta[0].payload==cuva.payload);
 }
 Message replacement=cuva;replacement.payload.back()=99;
 Bytes rewritten;assert(replace(packet.data(),packet.size(),{replacement},rewritten));
 assert(extract(rewritten.data(),rewritten.size(),0,meta)&&meta.size()==1&&meta[0].payload==replacement.payload);
 std::vector<Bytes> rewrittenNals;assert(split(rewritten.data(),rewritten.size(),0,rewrittenNals));
 std::vector<Message> kept;assert(messages(rewrittenNals.front(),kept));
 assert(kept.size()==2&&kept[0].payload==unrelated.payload&&kept[1].payload==staticHdr.payload);
 // 没CUVA的首帧同样插入；已有CUVA不能重复，其他HDR格式SEI不能被删除。
 auto bare=annex({{0x26,1,0x11,0x80}});assert(replace(bare.data(),bare.size(),{cuva},rewritten));
 assert(extract(rewritten.data(),rewritten.size(),0,meta)&&meta.size()==1);
 assert(replace(packet.data(),packet.size(),{},rewritten));assert(extract(rewritten.data(),rewritten.size(),0,meta)&&meta.empty());
 const uint8_t bad[]={0,0,0,9,78,1};assert(!extract(bad,sizeof(bad),4,meta));
 auto broken=annex({{78,1,4,255,255}});assert(!extract(broken.data(),broken.size(),0,meta));
 auto noFrame=annex({sei});assert(!replace(noFrame.data(),noFrame.size(),{cuva},rewritten));
 std::cout<<"CUVA原始负载、重复去除、混合SEI、1-4字节长度前缀与畸形输入检查通过\n";
}
