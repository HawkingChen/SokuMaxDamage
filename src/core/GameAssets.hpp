#pragma once
// Read-only access to the player's local TH12.3 archives. No game code,
// archive contents or third-party image libraries are bundled with the mod.
// Format reference: enebe-nb/shady-packer, src/Core/resource/image.cpp
// and the Tasofro package format. This implementation is self-contained.
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace whm::assets {
using Bytes = std::vector<std::uint8_t>;
namespace fs = std::filesystem;
inline constexpr const char *characters[] = {
    "reimu", "marisa", "sakuya", "alice", "patchouli", "youmu", "remilia",
    "yuyuko", "yukari", "suika", "udonge", "aya", "komachi", "iku",
    "tenshi", "sanae", "chirno", "meirin", "utsuho", "suwako"};
inline std::uint32_t le32(const Bytes &b, std::size_t p) {
    if (p > b.size() || b.size()-p < 4) throw std::runtime_error("truncated data");
    return b[p] | (std::uint32_t(b[p+1])<<8) | (std::uint32_t(b[p+2])<<16) |
           (std::uint32_t(b[p+3])<<24);
}
inline Bytes readAt(std::ifstream &f, std::uint64_t offset, std::size_t size) {
    f.clear(); f.seekg(static_cast<std::streamoff>(offset));
    Bytes bytes(size);
    if (size && !f.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("short archive read");
    return bytes;
}
struct Entry { std::string name; std::uint32_t offset, size; };
inline std::vector<Entry> readIndex(std::ifstream &f) {
    f.seekg(0, std::ios::end);
    const auto length=f.tellg();
    if (length<6) throw std::runtime_error("invalid archive header");
    const auto header=readAt(f,0,6);
    const unsigned count=header[0] | (unsigned(header[1])<<8);
    const auto size=le32(header,2);
    if (size>16*1024*1024 || std::uint64_t(size)+6>std::uint64_t(length))
        throw std::runtime_error("invalid archive index size");
    auto table=readAt(f,6,size);
    std::mt19937 random(size+6);
    unsigned key=0xc5, delta=0x83;
    for (auto &byte:table) {
        byte^=static_cast<std::uint8_t>(random());
        byte^=static_cast<std::uint8_t>(key);
        key=(key+delta)&255; delta=(delta+0x53)&255;
    }
    std::vector<Entry> entries;
    std::size_t p=0;
    for (unsigned i=0;i<count;++i) {
        if (p>table.size() || table.size()-p<9) throw std::runtime_error("short index entry");
        Entry e{"",le32(table,p),le32(table,p+4)};
        const unsigned n=table[p+8]; p+=9;
        if (n>table.size()-p || std::uint64_t(e.offset)+e.size>std::uint64_t(length))
            throw std::runtime_error("invalid archive entry bounds");
        e.name.assign(reinterpret_cast<const char*>(table.data()+p),n); p+=n;
        std::replace(e.name.begin(),e.name.end(),'\\','/');
        entries.push_back(e);
    }
    return entries;
}
inline Bytes readEntry(std::ifstream &f,const Entry &entry) {
    if (entry.size>16*1024*1024) throw std::runtime_error("image too large");
    auto bytes=readAt(f,entry.offset,entry.size);
    const auto key=static_cast<std::uint8_t>((entry.offset>>1)|0x23);
    for (auto &byte:bytes) byte^=key;
    return bytes;
}
// Whitelist exact resource paths. Never use archive filenames as disk paths.
inline std::string iconName(const std::string &path) {
    for (int ch=-1;ch<20;++ch) {
        const std::string prefix=std::string("data/card/")+(ch<0?"common":characters[ch])+"/card";
        if (path.size()!=prefix.size()+7 || path.compare(0,prefix.size(),prefix)!=0 ||
            path.substr(path.size()-4)!=".cv2") continue;
        int card=0;
        for (unsigned i=0;i<3;++i) {
            const char c=path[prefix.size()+i];
            if(c<'0'||c>'9') return {};
            card=card*10+c-'0';
        }
        return (ch<0?"common":std::to_string(ch))+"-"+std::to_string(card)+".png";
    }
    const std::string prefix="data/infoeffect/weatherName";
    if (path.compare(0,prefix.size(),prefix)==0 && path.size()>prefix.size()+4 &&
        path.substr(path.size()-4)==".cv2") {
        const std::string digits=path.substr(prefix.size(),path.size()-prefix.size()-4);
        if(digits.size()>3) return {};
        int weather=0;
        for(char c:digits) { if(c<'0'||c>'9')return {}; weather=weather*10+c-'0'; }
        if(weather<=22) return "weather-"+std::to_string(weather)+".png";
    }
    return {};
}
inline void be32(Bytes &out,std::uint32_t value) {
    for(int shift=24;shift>=0;shift-=8)out.push_back(static_cast<std::uint8_t>(value>>shift));
}
inline void chunk(Bytes &out,const char *type,const Bytes &data) {
    be32(out,static_cast<std::uint32_t>(data.size()));
    const auto begin=out.size();
    out.insert(out.end(),type,type+4); out.insert(out.end(),data.begin(),data.end());
    std::uint32_t crc=0xffffffff;
    for(auto i=begin;i<out.size();++i) {
        crc^=out[i];
        for(int b=0;b<8;++b)crc=(crc>>1)^((crc&1)?0xedb88320:0);
    }
    be32(out,~crc);
}
// CV2 stores BGRA rows with a padded width. 24-bit CV2 also uses 4 bytes
// per pixel, with an unused alpha byte. Indexed sprites need a palette and
// are deliberately rejected; the original card/weather artwork is 32-bit.
inline Bytes cv2ToPng(const Bytes &cv) {
    if(cv.size()<17 || (cv[0]!=24&&cv[0]!=32))throw std::runtime_error("unsupported CV2 format");
    const auto w=le32(cv,1),h=le32(cv,5),stride=le32(cv,9);
    if(!w||!h||w>1024||h>1024||stride<w||stride>1024||
       std::uint64_t(stride)*h*4+17>cv.size())throw std::runtime_error("invalid CV2 dimensions");
    Bytes scan;
    scan.reserve((w*4+1)*h);
    for(unsigned y=0;y<h;++y) {
        scan.push_back(0); // PNG filter: none
        for(unsigned x=0;x<w;++x) {
            const auto p=17+(std::size_t(y)*stride+x)*4;
            scan.insert(scan.end(),{cv[p+2],cv[p+1],cv[p],cv[0]==32?cv[p+3]:std::uint8_t(255)});
        }
    }
    // RFC 1950/1951 zlib stream with stored blocks. Small card images need
    // no external compressor; PNG output is only a local disk cache.
    Bytes zlib{0x78,0x01};
    for(std::size_t p=0;p<scan.size();) {
        const auto n=std::min<std::size_t>(65535,scan.size()-p);
        zlib.push_back(p+n==scan.size()?1:0);
        zlib.push_back(n&255);zlib.push_back((n>>8)&255);
        zlib.push_back((~n)&255);zlib.push_back(((~n)>>8)&255);
        zlib.insert(zlib.end(),scan.begin()+p,scan.begin()+p+n);p+=n;
    }
    std::uint32_t a=1,b=0;
    for(auto c:scan){a=(a+c)%65521;b=(b+a)%65521;}
    be32(zlib,(b<<16)|a);
    Bytes png{137,80,78,71,13,10,26,10},ihdr;
    be32(ihdr,w);be32(ihdr,h);ihdr.insert(ihdr.end(),{8,6,0,0,0});
    chunk(png,"IHDR",ihdr);chunk(png,"IDAT",zlib);chunk(png,"IEND",{});
    return png;
}
struct ExtractionResult { unsigned written=0,preserved=0,failed=0,archives=0; };
inline ExtractionResult extractIcons(const fs::path &game,const fs::path &icons) {
    struct Source { fs::path archive; Entry entry; };
    std::map<std::string,Source> latest;
    ExtractionResult result;
    for(const auto *name:{"th123a.dat","th123b.dat","th123c.dat"}) {
        std::ifstream file(game/name,std::ios::binary);
        if(!file)continue;
        try {
            for(const auto &e:readIndex(file)) {
                const auto output=iconName(e.name);
                if(!output.empty())latest[output]={game/name,e}; // later patch wins
            }
            ++result.archives;
        } catch(const std::exception &){++result.failed;}
    }
    std::error_code ec;
    fs::create_directories(icons,ec);
    if(ec) {result.failed+=static_cast<unsigned>(latest.size());return result;}
    for(const auto &[name,source]:latest) {
        const auto dest=icons/name;
        if(fs::exists(dest,ec)){++result.preserved;continue;}
        const fs::path temp=dest.native()+fs::path(".tmp").native();
        try {
            std::ifstream file(source.archive,std::ios::binary);
            const auto png=cv2ToPng(readEntry(file,source.entry));
            std::ofstream out(temp,std::ios::binary|std::ios::trunc);
            out.write(reinterpret_cast<const char*>(png.data()),png.size());out.close();
            if(!out)throw std::runtime_error("cannot write icon cache");
            // Recheck before rename so an existing/custom icon stays intact.
            if(fs::exists(dest)){fs::remove(temp);++result.preserved;continue;}
            fs::rename(temp,dest);++result.written;
        } catch(const std::exception &){fs::remove(temp,ec);++result.failed;}
    }
    return result;
}
} // namespace whm::assets
