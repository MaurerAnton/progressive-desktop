// src/core/crypto/sas.hpp — OlmSAS wrapper for Short Authentication String verification.
#pragma once
#include <string>
#include <cstdint>

namespace progressive::desktop {

struct SasSession {
    void* sas = nullptr;
    std::string ourPubkey;
    bool theirKeySet = false;
    bool valid = false;
    ~SasSession();
};

SasSession sasCreate();
bool sasSetTheirKey(SasSession& sas, const std::string& theirPubkeyBase64);
std::string sasGenerateBytes(SasSession& sas, const std::string& info);
std::string sasCalculateMac(SasSession& sas, const std::string& message,
                              const std::string& info);
bool sasVerifyMac(SasSession& sas, const std::string& theirMacBase64,
                   const std::string& message, const std::string& info);

} // namespace progressive::desktop
