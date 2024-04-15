#include "Signal.h"

namespace SST::VerilatorSST{
    template<typename T>
    T Signal::getUIntScalarInternal(signal_width_t nBytes, signal_depth_t offset){
        T ret = 0;
        for(auto i = 0; i<nBytes; i++){
            PLI_BYTE8 byte = storage[(offset*nBytes) + i];
            uint8_t castSafeByte = static_cast<uint8_t>(byte);
            uint8_t padSafeByte = (castSafeByte == ' ') ? 0 : castSafeByte; //TODO https://github.com/verilator/verilator/issues/5036

            ret = ret << 8;
            ret |= padSafeByte & 255;
        }
        return ret;
    }

    template<typename T>
    T Signal::getUIntScalar() {
        static_assert(std::is_unsigned_v<T> == true);
        signal_width_t nBytes = getNumBytes();
        assert(sizeof(T) >= nBytes*depth && "uint type must be larger than signal width");

        T ret = getUIntScalarInternal<T>(nBytes,0);
        return ret;
    }

    template<typename T>
    T* Signal::getUIntVector() {
        static_assert(std::is_unsigned_v<T> == true);
        assert(sizeof(T)*8 >= nBits && "uint type must be larger than word width");

        T* ret = new T[depth]();
        
        auto nBytesPerWord = getNumBytes();
        for(auto i=0;i<depth;i++){
            ret[i] = getUIntScalarInternal<T>(nBytesPerWord,i);
        }

        return ret;
    }
}
