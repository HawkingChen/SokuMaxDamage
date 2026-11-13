#include "GameAssets.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace whm::assets;
Bytes image(unsigned red) {
    Bytes b{32,1,0,0,0,1,0,0,0,2,0,0,0,0,0,0,0};
    b.insert(b.end(),{30,20,static_cast<std::uint8_t>(red),128,0,0,0,0});
    return b;
}
void archive(const fs::path &path,const Bytes &payload) {
    const std::string name="data/card/common/card018.cv2";
    const unsigned size=9+static_cast<unsigned>(name.size()),offset=6+size;
    auto little=[](Bytes &b,unsigned n){for(int i=0;i<4;++i)b.push_back((n>>(i*8))&255);};
    Bytes header{1,0};little(header,size);
    Bytes index;little(index,offset);little(index,static_cast<unsigned>(payload.size()));
    index.push_back(static_cast<std::uint8_t>(name.size()));index.insert(index.end(),name.begin(),name.end());
    std::mt19937 random(offset);unsigned key=0xc5,delta=0x83;
    for(auto &c:index){c^=static_cast<std::uint8_t>(random())^key;key=(key+delta)&255;delta=(delta+0x53)&255;}
    std::ofstream f(path,std::ios::binary);
    f.write(reinterpret_cast<const char*>(header.data()),header.size());
    f.write(reinterpret_cast<const char*>(index.data()),index.size());
    for(auto c:payload)f.put(c^((offset>>1)|0x23));
}
int main(int argc,char **argv) {
    if(argc==3) {
        const auto r=extractIcons(fs::u8path(argv[1]),fs::u8path(argv[2]));
        std::cout<<"archives="<<r.archives<<" written="<<r.written<<" preserved="<<r.preserved<<" failed="<<r.failed<<"\n";
        return r.failed||!r.archives||(!r.written&&!r.preserved);
    }
    assert(iconName("data/card/common/card018.cv2")=="common-18.png");
    assert(iconName("data/card/meirin/card109.cv2")=="17-109.png");
    assert(iconName("data/infoeffect/weatherName004.cv2")=="weather-4.png");
    assert(iconName("data/card/../../evil.cv2").empty());
    assert(iconName("data/card/reimu/card0x1.cv2").empty());
    for(const Bytes b:{Bytes{},Bytes{32},Bytes(17,0)}) {
        bool threw=false;try{cv2ToPng(b);}catch(const std::exception &){threw=true;}assert(threw);
    }
    const auto png=cv2ToPng(image(10));
    assert(png[0]==137&&png[1]=='P'&&png[25]==6); // RGBA, padded pixel excluded
    auto oversized=image(10);oversized[3]=255;
    bool threw=false;try{cv2ToPng(oversized);}catch(const std::exception &){threw=true;}assert(threw);
    const auto root=fs::temp_directory_path()/
        ("whm-assets-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"game");
    const auto unicodeIcons=root/fs::u8path("图标 日本語");
    archive(root/"game/th123a.dat",image(10));
    archive(root/"game/th123c.dat",image(100));
    auto r=extractIcons(root/"game",unicodeIcons);
    assert(r.archives==2&&r.written==1&&r.failed==0);
    std::ifstream f(unicodeIcons/"common-18.png",std::ios::binary);
    const Bytes actual((std::istreambuf_iterator<char>(f)),{});f.close();
    assert(actual==cv2ToPng(image(100))); // patched resource wins
    r=extractIcons(root/"game",unicodeIcons);
    assert(r.preserved==1&&r.written==0&&r.failed==0);
    // Corrupt archives are rejected; existing custom artwork stays untouched.
    std::ofstream(root/"game/th123b.dat",std::ios::binary)<<"broken";
    r=extractIcons(root/"game",unicodeIcons);assert(r.failed==1&&r.preserved==1);
    fs::remove_all(root);
    std::cout<<"Asset extraction, bounds, patch precedence and cache preservation passed\n";
}
