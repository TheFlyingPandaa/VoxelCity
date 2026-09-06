#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
namespace vc::binary {
template<class T> void write(std::ostream& out,T value) {
    static_assert(std::is_arithmetic_v<T>);
    auto bytes=std::bit_cast<std::array<unsigned char,sizeof(T)>>(value);
    out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if(!out)throw std::runtime_error("Could not write city state");
}
template<class T> T read(std::istream& in) {
    std::array<unsigned char,sizeof(T)> bytes{};in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(!in)throw std::runtime_error("Truncated city state");return std::bit_cast<T>(bytes);
}
inline void text(std::ostream& out,const std::string& value) {write(out,uint32_t(value.size()));out.write(value.data(),value.size());}
inline std::string text(std::istream& in,uint32_t maximum=100000) {auto n=read<uint32_t>(in);if(n>maximum)throw std::runtime_error("Invalid string size");std::string s(n,'\0');in.read(s.data(),n);if(!in)throw std::runtime_error("Truncated text");return s;}
template<class T> void vector(std::ostream& out,const std::vector<T>& v) {write(out,uint32_t(v.size()));for(T x:v)write(out,x);}
template<class T> void vector(std::istream& in,std::vector<T>& v,size_t maximum) {auto n=read<uint32_t>(in);if(n>maximum)throw std::runtime_error("Invalid array size");v.resize(n);for(auto& x:v)x=read<T>(in);}
}
