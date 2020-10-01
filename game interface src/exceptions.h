#pragma once

#include <exception>
#include <Windows.h>

// SE Exception wrapper class
// Retrieved from https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/set-se-translator
class SE_Exception : public std::exception {
private:
    const unsigned int nSE;
public:
    SE_Exception() noexcept : SE_Exception{ 0 } {}
    SE_Exception(unsigned int n) noexcept : nSE{ n } {}
    unsigned int getSeNumber() const noexcept { return nSE; }
};

// RAII syntax exception translator function
// Retrieved from https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/set-se-translator
class Scoped_SE_Translator {
private:
    const _se_translator_function old_SE_translator;
public:
    Scoped_SE_Translator(_se_translator_function new_SE_translator) noexcept
        : old_SE_translator{
            _set_se_translator(new_SE_translator)
    } { }

    ~Scoped_SE_Translator() noexcept {
        _set_se_translator(old_SE_translator);
    }
};

#pragma unmanaged
void SETranslator(unsigned int u, PEXCEPTION_POINTERS) {
    throw SE_Exception(u);
}