// This file is a part of Julia. License is MIT: https://julialang.org/license

// ARM (AArch32/AArch64) specific processor detection and dispatch

#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <set>
#include <fstream>
#include <algorithm>

// This nesting is required to allow compilation on musl
#define USE_DYN_GETAUXVAL
#if (defined(_OS_LINUX_) || defined(_OS_FREEBSD_)) && defined(_CPU_AARCH64_)
#  undef USE_DYN_GETAUXVAL
#  include <sys/auxv.h>
#elif defined(__GLIBC_PREREQ)
#  if __GLIBC_PREREQ(2, 16)
#    undef USE_DYN_GETAUXVAL
#    include <sys/auxv.h>
#  endif
#elif defined _CPU_AARCH64_ && defined _OS_DARWIN_
#include <sys/sysctl.h>
#include <string.h>
#endif

namespace ARM {
enum class CPU : uint32_t {
    generic = 0,

    // Architecture targets
    armv7_a,
    armv7_m,
    armv7e_m,
    armv7_r,
    armv8_a,
    armv8_m_base,
    armv8_m_main,
    armv8_r,
    armv8_1_a,
    armv8_2_a,
    armv8_3_a,
    armv8_4_a,
    armv8_5_a,
    armv8_6_a,

    // ARM
    // armv6l
    arm_mpcore,
    arm_1136jf_s,
    arm_1156t2f_s,
    arm_1176jzf_s,
    arm_cortex_m0,
    arm_cortex_m1,
    // armv7ml
    arm_cortex_m3,
    arm_cortex_m4,
    arm_cortex_m7,
    // armv7l
    arm_cortex_a5,
    arm_cortex_a7,
    arm_cortex_a8,
    arm_cortex_a9,
    arm_cortex_a12,
    arm_cortex_a15,
    arm_cortex_a17,
    arm_cortex_r4,
    arm_cortex_r5,
    arm_cortex_r7,
    arm_cortex_r8,
    // armv8ml
    arm_cortex_m23,
    arm_cortex_m33,
    // armv8l
    arm_cortex_a32,
    arm_cortex_r52,
    // aarch64
    arm_cortex_a34,
    arm_cortex_a35,
    arm_cortex_a53,
    arm_cortex_a55,
    arm_cortex_a57,
    arm_cortex_a65,
    arm_cortex_a65ae,
    arm_cortex_a72,
    arm_cortex_a73,
    arm_cortex_a75,
    arm_cortex_a76,
    arm_cortex_a76ae,
    arm_cortex_a77,
    arm_cortex_a78,
    arm_cortex_x1,
    arm_neoverse_e1,
    arm_neoverse_n1,
    arm_neoverse_v1,
    arm_neoverse_n2,

    // Cavium
    // aarch64
    cavium_thunderx,
    cavium_thunderx88,
    cavium_thunderx88p1,
    cavium_thunderx81,
    cavium_thunderx83,
    cavium_thunderx2t99,
    cavium_thunderx2t99p1,
    cavium_octeontx2,
    cavium_octeontx2t98,
    cavium_octeontx2t96,
    cavium_octeontx2f95,
    cavium_octeontx2f95n,
    cavium_octeontx2f95mm,

    // Fujitsu
    // aarch64
    fujitsu_a64fx,

    // HiSilicon
    // aarch64
    hisilicon_tsv110,

    // Huaxingtong
    // aarch64
    hxt_phecda,

    // NVIDIA
    // aarch64
    nvidia_denver1,
    nvidia_denver2,
    nvidia_carmel,

    // AppliedMicro
    // aarch64
    apm_xgene1,
    apm_xgene2,
    apm_xgene3,

    // Qualcomm
    // armv7l
    qualcomm_scorpion,
    qualcomm_krait,
    // aarch64
    qualcomm_kyro,
    qualcomm_falkor,
    qualcomm_saphira,

    // Samsung
    // aarch64
    samsung_exynos_m1,
    samsung_exynos_m2,
    samsung_exynos_m3,
    samsung_exynos_m4,
    samsung_exynos_m5,

    // Apple
    // armv7l
    apple_swift,
    // aarch64
    apple_a7, // cyclone
    apple_a8, // typhoon
    apple_a9, // twister
    apple_a10, // hurricane
    apple_a11,
    apple_a12,
    apple_a13,
    apple_a14,
    apple_a15,
    apple_a16,
    apple_a17,
    apple_m1,
    apple_m2,
    apple_m3,
    apple_m4,
    apple_s4,
    apple_s5,

    // Marvell
    // armv7l
    marvell_pj4,
    // aarch64
    marvell_thunderx3t110,

    // Intel
    // armv7l
    intel_3735d,
};

#ifdef _CPU_AARCH64_
static constexpr size_t feature_sz = 3;
static constexpr FeatureName feature_names[] = {
#define JL_FEATURE_DEF(name, bit, llvmver) {#name, bit, llvmver},
#define JL_FEATURE_DEF_NAME(name, bit, llvmver, str) {str, bit, llvmver},
#include "features_aarch64.h"
#undef JL_FEATURE_DEF
#undef JL_FEATURE_DEF_NAME
};
static constexpr uint32_t nfeature_names = sizeof(feature_names) / sizeof(FeatureName);

template<typename... Args>
static inline constexpr FeatureList<feature_sz> get_feature_masks(Args... args)
{
    return ::get_feature_masks<feature_sz>(args...);
}

#define JL_FEATURE_DEF_NAME(name, bit, llvmver, str) JL_FEATURE_DEF(name, bit, llvmver)
static constexpr auto feature_masks = get_feature_masks(
#define JL_FEATURE_DEF(name, bit, llvmver) bit,
#include "features_aarch64.h"
#undef JL_FEATURE_DEF
    -1);
static const auto real_feature_masks =
    feature_masks & FeatureList<feature_sz>{{UINT32_MAX, UINT32_MAX, 0}};

namespace Feature {
enum : uint32_t {
#define JL_FEATURE_DEF(name, bit, llvmver) name = bit,
#include "features_aarch64.h"
#undef JL_FEATURE_DEF
};
#undef JL_FEATURE_DEF_NAME
// This does not cover all dependencies (e.g. the ones that depends on arm versions)
static constexpr FeatureDep deps[] = {
    {rcpc_immo, rcpc},
    {sha3, sha2},
    // {sha512, sha3},
    {ccdp, ccpp},
    {sve, fullfp16},
    {fp16fml, fullfp16},
    {altnzcv, flagm},
    {sve2, sve},
    {sve2_aes, sve2},
    {sve2_aes, aes},
    {sve2_bitperm, sve2},
    {sve2_sha3, sve2},
    {sve2_sha3, sha3},
    {sve2_sm4, sve2},
    {sve2_sm4, sm4},
    {f32mm, sve},
    {f64mm, sve},
};

constexpr auto generic = get_feature_masks();
constexpr auto armv8a_crc = get_feature_masks(crc);
constexpr auto armv8a_crc_crypto = armv8a_crc | get_feature_masks(aes, sha2);
constexpr auto armv8_1a = armv8a_crc | get_feature_masks(v8_1a, lse, rdm); // lor
constexpr auto armv8_1a_crypto = armv8_1a | get_feature_masks(aes, sha2);
constexpr auto armv8_2a = armv8_1a | get_feature_masks(v8_2a, ccpp);
constexpr auto armv8_2a_crypto = armv8_2a | get_feature_masks(aes, sha2);
constexpr auto armv8_3a = armv8_2a | get_feature_masks(v8_3a, jsconv, complxnum, rcpc);
constexpr auto armv8_3a_crypto = armv8_3a | get_feature_masks(aes, sha2);
constexpr auto armv8_4a = armv8_3a | get_feature_masks(v8_4a, dit, rcpc_immo, flagm);
constexpr auto armv8_4a_crypto = armv8_4a | get_feature_masks(aes, sha2);
constexpr auto armv8_5a = armv8_4a | get_feature_masks(v8_5a, sb, ccdp, altnzcv, fptoint);
constexpr auto armv8_5a_crypto = armv8_5a | get_feature_masks(aes, sha2);
constexpr auto armv8_6a = armv8_5a | get_feature_masks(v8_6a, i8mm, bf16);

// For ARM cores, the features required can be found in the technical reference manual
// The relevant register values and the features they are related to are:
// ID_AA64ISAR0_EL1:
//     .AES: aes, pmull
//     .SHA1: sha1
//     .SHA2: sha2, sha512
//     .CRC32: crc
//     .Atomic: les
//     .RDM: rdm
//     .SHA3: sha3
//     .SM3: sm3 (sm4)
//     .SM4: sm4
//     .DP: dotprod
//     .FHM: fp16fml
//     .TS: flagm, altnzcz
//     .RNDR: rand

// ID_AA64ISAR1_EL1
//     .JSCVT: jsconv
//     .FCMA: complxnum
//     .LRCPC: rcpc, rcpc_immo
//     .DPB: ccpp, ccdp
//     .SB: sb
//     .APA/.API: paca (pa)
//     .GPA/.GPI: paga (pa)
//     .FRINTTS: fptoint
//     .I8MM: i8mm
//     .BF16: bf16
//     .DGH: dgh

// ID_AA64PFR0_EL1
//     .FP: fullfp16
//     .SVE: sve
//     .DIT: dit
//     .BT: bti

// ID_AA64PFR1_EL1
//     .SSBS: ssbs
//     .MTE: mte

// ID_AA64MMFR2_EL1.AT: uscat

// ID_AA64ZFR0_EL1
//     .SVEVer: sve2
//     .AES: sve2-aes, sve2-pmull
//     .BitPerm: sve2-bitperm
//     .SHA3: sve2-sha3
//     .SM4: sve2-sm4
//     .F32MM: f32mm
//     .F64MM: f64mm

constexpr auto arm_cortex_a34 = armv8a_crc;
constexpr auto arm_cortex_a35 = armv8a_crc;
constexpr auto arm_cortex_a53 = armv8a_crc;
constexpr auto arm_cortex_a55 = armv8_2a | get_feature_masks(dotprod, rcpc, fullfp16, ssbs);
constexpr auto arm_cortex_a57 = armv8a_crc;
constexpr auto arm_cortex_a65 = armv8_2a | get_feature_masks(rcpc, fullfp16, ssbs);
constexpr auto arm_cortex_a72 = armv8a_crc;
constexpr auto arm_cortex_a73 = armv8a_crc;
constexpr auto arm_cortex_a75 = armv8_2a | get_feature_masks(dotprod, rcpc, fullfp16);
constexpr auto arm_cortex_a76 = armv8_2a | get_feature_masks(dotprod, rcpc, fullfp16, ssbs);
constexpr auto arm_cortex_a77 = armv8_2a | get_feature_masks(dotprod, rcpc, fullfp16, ssbs);
constexpr auto arm_cortex_a78 = armv8_2a | get_feature_masks(dotprod, rcpc, fullfp16, ssbs); // spe
constexpr auto arm_cortex_x1 = armv8_2a | get_feature_masks(dotprod, rcpc, fullfp16, ssbs); // spe
constexpr auto arm_neoverse_e1 = armv8_2a | get_feature_masks(rcpc, fullfp16, ssbs);
constexpr auto arm_neoverse_n1 = armv8_2a | get_feature_masks(dotprod, rcpc, fullfp16, ssbs);
constexpr auto arm_neoverse_v1 = armv8_4a | get_feature_masks(sve, i8mm, bf16, fullfp16, ssbs, rand);
constexpr auto arm_neoverse_n2 = armv8_5a | get_feature_masks(sve, i8mm, bf16, fullfp16, sve2,
                                                              sve2_bitperm, rand, mte);
constexpr auto cavium_thunderx = armv8a_crc_crypto;
constexpr auto cavium_thunderx88 = armv8a_crc_crypto;
constexpr auto cavium_thunderx88p1 = armv8a_crc_crypto;
constexpr auto cavium_thunderx81 = armv8a_crc_crypto;
constexpr auto cavium_thunderx83 = armv8a_crc_crypto;
constexpr auto cavium_thunderx2t99 = armv8_1a_crypto;
constexpr auto cavium_thunderx2t99p1 = cavium_thunderx2t99;
constexpr auto cavium_octeontx2 = armv8_2a_crypto;
constexpr auto fujitsu_a64fx = armv8_2a | get_feature_masks(sha2, fullfp16, sve, complxnum);
constexpr auto hisilicon_tsv110 = armv8_2a_crypto | get_feature_masks(dotprod, fullfp16);
constexpr auto hxt_phecda = armv8a_crc_crypto;
constexpr auto marvell_thunderx3t110 = armv8_3a_crypto;
constexpr auto nvidia_denver1 = generic; // TODO? (crc, crypto)
constexpr auto nvidia_denver2 = armv8a_crc_crypto;
constexpr auto nvidia_carmel = armv8_2a_crypto | get_feature_masks(fullfp16);
constexpr auto apm_xgene1 = generic;
constexpr auto apm_xgene2 = generic; // TODO?
constexpr auto apm_xgene3 = generic; // TODO?
constexpr auto qualcomm_kyro = armv8a_crc_crypto;
constexpr auto qualcomm_falkor = armv8a_crc_crypto | get_feature_masks(rdm);
constexpr auto qualcomm_saphira = armv8_4a_crypto;
constexpr auto samsung_exynos_m1 = armv8a_crc_crypto;
constexpr auto samsung_exynos_m2 = armv8a_crc_crypto;
constexpr auto samsung_exynos_m3 = armv8a_crc_crypto;
constexpr auto samsung_exynos_m4 = armv8_2a_crypto | get_feature_masks(dotprod, fullfp16);
constexpr auto samsung_exynos_m5 = samsung_exynos_m4;
constexpr auto apple_a7 = armv8a_crc_crypto;
constexpr auto apple_a10 = armv8a_crc_crypto | get_feature_masks(rdm);
constexpr auto apple_a11 = armv8_2a_crypto | get_feature_masks(fullfp16);
constexpr auto apple_a12 = armv8_3a_crypto | get_feature_masks(fullfp16);
constexpr auto apple_a13 = armv8_4a_crypto | get_feature_masks(fp16fml, fullfp16, sha3);
constexpr auto apple_a14 = armv8_5a_crypto | get_feature_masks(dotprod,fp16fml, fullfp16, sha3);
constexpr auto apple_a15 = armv8_5a_crypto | get_feature_masks(dotprod,fp16fml, fullfp16, sha3, i8mm, bf16);
constexpr auto apple_a16 = armv8_5a_crypto | get_feature_masks(dotprod,fp16fml, fullfp16, sha3, i8mm, bf16);
constexpr auto apple_a17 = armv8_5a_crypto | get_feature_masks(dotprod,fp16fml, fullfp16, sha3, i8mm, bf16);
constexpr auto apple_m1 = armv8_5a_crypto | get_feature_masks(dotprod,fp16fml, fullfp16, sha3);
constexpr auto apple_m2 = armv8_5a_crypto | get_feature_masks(dotprod,fp16fml, fullfp16, sha3, i8mm, bf16);
constexpr auto apple_m3 = armv8_5a_crypto | get_feature_masks(dotprod,fp16fml, fullfp16, sha3, i8mm, bf16);
constexpr auto apple_m4 = armv8_5a_crypto | get_feature_masks(dotprod,fp16fml, fullfp16, sha3, i8mm, bf16);
// Features based on https://github.com/llvm/llvm-project/blob/82507f1798768280cf5d5aab95caaafbc7fe6f47/llvm/include/llvm/Support/AArch64TargetParser.def
// and sysctl -a hw.optional
constexpr auto apple_s4 = apple_a12;
constexpr auto apple_s5 = apple_a12;

}

static constexpr CPUSpec<CPU, feature_sz> cpus[] = {
    {"generic", CPU::generic, CPU::generic, 0, Feature::generic},
    {"armv8.1-a", CPU::armv8_1_a, CPU::generic, 0, Feature::armv8_1a},
    {"armv8.2-a", CPU::armv8_2_a, CPU::generic, 0, Feature::armv8_2a},
    {"armv8.3_a", CPU::armv8_3_a, CPU::generic, 0, Feature::armv8_3a},
    {"armv8.4-a", CPU::armv8_4_a, CPU::generic, 0, Feature::armv8_4a},
    {"armv8.5-a", CPU::armv8_5_a, CPU::generic, 0, Feature::armv8_5a},
    {"armv8.6_a", CPU::armv8_6_a, CPU::generic, 0, Feature::armv8_6a},
    {"cortex-a34", CPU::arm_cortex_a34, CPU::arm_cortex_a35, 110000, Feature::arm_cortex_a34},
    {"cortex-a35", CPU::arm_cortex_a35, CPU::generic, 0, Feature::arm_cortex_a35},
    {"cortex-a53", CPU::arm_cortex_a53, CPU::generic, 0, Feature::arm_cortex_a53},
    {"cortex-a55", CPU::arm_cortex_a55, CPU::generic, 0, Feature::arm_cortex_a55},
    {"cortex-a57", CPU::arm_cortex_a57, CPU::generic, 0, Feature::arm_cortex_a57},
    {"cortex-a65", CPU::arm_cortex_a65, CPU::arm_cortex_a75, 100000, Feature::arm_cortex_a65},
    {"cortex-a65ae", CPU::arm_cortex_a65ae, CPU::arm_cortex_a75, 100000, Feature::arm_cortex_a65},
    {"cortex-a72", CPU::arm_cortex_a72, CPU::generic, 0, Feature::arm_cortex_a72},
    {"cortex-a73", CPU::arm_cortex_a73, CPU::generic, 0, Feature::arm_cortex_a73},
    {"cortex-a75", CPU::arm_cortex_a75, CPU::generic, 0, Feature::arm_cortex_a75},
    {"cortex-a76", CPU::arm_cortex_a76, CPU::generic, 0, Feature::arm_cortex_a76},
    {"cortex-a76ae", CPU::arm_cortex_a76ae, CPU::generic, 0, Feature::arm_cortex_a76},
    {"cortex-a77", CPU::arm_cortex_a77, CPU::arm_cortex_a76, 110000, Feature::arm_cortex_a77},
    {"cortex-a78", CPU::arm_cortex_a78, CPU::arm_cortex_a77, 110000, Feature::arm_cortex_a78},
    {"cortex-x1", CPU::arm_cortex_x1, CPU::arm_cortex_a78, 110000, Feature::arm_cortex_x1},
    {"neoverse-e1", CPU::arm_neoverse_e1, CPU::arm_cortex_a76, 100000, Feature::arm_neoverse_e1},
    {"neoverse-n1", CPU::arm_neoverse_n1, CPU::arm_cortex_a76, 100000, Feature::arm_neoverse_n1},
    {"neoverse-v1", CPU::arm_neoverse_v1, CPU::arm_neoverse_n1, UINT32_MAX, Feature::arm_neoverse_v1},
    {"neoverse-n2", CPU::arm_neoverse_n2, CPU::arm_neoverse_n1, UINT32_MAX, Feature::arm_neoverse_n2},
    {"thunderx", CPU::cavium_thunderx, CPU::generic, 0, Feature::cavium_thunderx},
    {"thunderxt88", CPU::cavium_thunderx88, CPU::generic, 0, Feature::cavium_thunderx88},
    {"thunderxt88p1", CPU::cavium_thunderx88p1, CPU::cavium_thunderx88, UINT32_MAX,
     Feature::cavium_thunderx88p1},
    {"thunderxt81", CPU::cavium_thunderx81, CPU::generic, 0, Feature::cavium_thunderx81},
    {"thunderxt83", CPU::cavium_thunderx83, CPU::generic, 0, Feature::cavium_thunderx83},
    {"thunderx2t99", CPU::cavium_thunderx2t99, CPU::generic, 0, Feature::cavium_thunderx2t99},
    {"thunderx2t99p1", CPU::cavium_thunderx2t99p1, CPU::cavium_thunderx2t99, UINT32_MAX,
     Feature::cavium_thunderx2t99p1},
    {"octeontx2", CPU::cavium_octeontx2, CPU::arm_cortex_a57, UINT32_MAX,
     Feature::cavium_octeontx2},
    {"octeontx2t98", CPU::cavium_octeontx2t98, CPU::arm_cortex_a57, UINT32_MAX,
     Feature::cavium_octeontx2},
    {"octeontx2t96", CPU::cavium_octeontx2t96, CPU::arm_cortex_a57, UINT32_MAX,
     Feature::cavium_octeontx2},
    {"octeontx2f95", CPU::cavium_octeontx2f95, CPU::arm_cortex_a57, UINT32_MAX,
     Feature::cavium_octeontx2},
    {"octeontx2f95n", CPU::cavium_octeontx2f95n, CPU::arm_cortex_a57, UINT32_MAX,
     Feature::cavium_octeontx2},
    {"octeontx2f95mm", CPU::cavium_octeontx2f95mm, CPU::arm_cortex_a57, UINT32_MAX,
     Feature::cavium_octeontx2},
    {"a64fx", CPU::fujitsu_a64fx, CPU::generic, 110000, Feature::fujitsu_a64fx},
    {"tsv110", CPU::hisilicon_tsv110, CPU::generic, 0, Feature::hisilicon_tsv110},
    {"phecda", CPU::hxt_phecda, CPU::qualcomm_falkor, UINT32_MAX, Feature::hxt_phecda},
    {"denver1", CPU::nvidia_denver1, CPU::generic, UINT32_MAX, Feature::nvidia_denver1},
    {"denver2", CPU::nvidia_denver2, CPU::generic, UINT32_MAX, Feature::nvidia_denver2},
    {"carmel", CPU::nvidia_carmel, CPU::generic, 110000, Feature::nvidia_carmel},
    {"xgene1", CPU::apm_xgene1, CPU::generic, UINT32_MAX, Feature::apm_xgene1},
    {"xgene2", CPU::apm_xgene2, CPU::generic, UINT32_MAX, Feature::apm_xgene2},
    {"xgene3", CPU::apm_xgene3, CPU::generic, UINT32_MAX, Feature::apm_xgene3},
    {"kyro", CPU::qualcomm_kyro, CPU::generic, 0, Feature::qualcomm_kyro},
    {"falkor", CPU::qualcomm_falkor, CPU::generic, 0, Feature::qualcomm_falkor},
    {"saphira", CPU::qualcomm_saphira, CPU::generic, 0, Feature::qualcomm_saphira},
    {"exynos-m1", CPU::samsung_exynos_m1, CPU::generic, UINT32_MAX, Feature::samsung_exynos_m1},
    {"exynos-m2", CPU::samsung_exynos_m2, CPU::generic, UINT32_MAX, Feature::samsung_exynos_m2},
    {"exynos-m3", CPU::samsung_exynos_m3, CPU::generic, 0, Feature::samsung_exynos_m3},
    {"exynos-m4", CPU::samsung_exynos_m4, CPU::generic, 0, Feature::samsung_exynos_m4},
    {"exynos-m5", CPU::samsung_exynos_m5, CPU::samsung_exynos_m4, 110000,
     Feature::samsung_exynos_m5},
    {"apple-a7", CPU::apple_a7, CPU::generic, 100000, Feature::apple_a7},
    {"apple-a8", CPU::apple_a8, CPU::generic, 100000, Feature::apple_a7},
    {"apple-a9", CPU::apple_a9, CPU::generic, 100000, Feature::apple_a7},
    {"apple-a10", CPU::apple_a10, CPU::generic, 100000, Feature::apple_a10},
    {"apple-a11", CPU::apple_a11, CPU::generic, 100000, Feature::apple_a11},
    {"apple-a12", CPU::apple_a12, CPU::generic, 100000, Feature::apple_a12},
    {"apple-a13", CPU::apple_a13, CPU::generic, 100000, Feature::apple_a13},
    {"apple-a14", CPU::apple_a14, CPU::apple_a13, 120000, Feature::apple_a14},
    {"apple-a15", CPU::apple_a15, CPU::apple_a14, 160000, Feature::apple_a15},
    {"apple-a16", CPU::apple_a16, CPU::apple_a14, 160000, Feature::apple_a16},
    {"apple-a17", CPU::apple_a17, CPU::apple_a16, 190000, Feature::apple_a17},
    {"apple-m1", CPU::apple_m1, CPU::apple_a14, 130000, Feature::apple_m1},
    {"apple-m2", CPU::apple_m2, CPU::apple_m1, 160000, Feature::apple_m2},
    {"apple-m3", CPU::apple_m3, CPU::apple_m2, 180000, Feature::apple_m3},
    {"apple-m4", CPU::apple_m4, CPU::apple_m3, 190000, Feature::apple_m4},
    {"apple-s4", CPU::apple_s4, CPU::generic, 100000, Feature::apple_s4},
    {"apple-s5", CPU::apple_s5, CPU::generic, 100000, Feature::apple_s5},
    {"thunderx3t110", CPU::marvell_thunderx3t110, CPU::cavium_thunderx2t99, 110000,
     Feature::marvell_thunderx3t110},
};
#else
static constexpr size_t feature_sz = 3;
static constexpr FeatureName feature_names[] = {
#define JL_FEATURE_DEF(name, bit, llvmver) {#name, bit, llvmver},
#define JL_FEATURE_DEF_NAME(name, bit, llvmver, str) {str, bit, llvmver},
#include "features_aarch32.h"
#undef JL_FEATURE_DEF
#undef JL_FEATURE_DEF_NAME
};
static constexpr uint32_t nfeature_names = sizeof(feature_names) / sizeof(FeatureName);

template<typename... Args>
static inline constexpr FeatureList<feature_sz> get_feature_masks(Args... args)
{
    return ::get_feature_masks<feature_sz>(args...);
}

#define JL_FEATURE_DEF_NAME(name, bit, llvmver, str) JL_FEATURE_DEF(name, bit, llvmver)
static constexpr auto feature_masks = get_feature_masks(
#define JL_FEATURE_DEF(name, bit, llvmver) bit,
#include "features_aarch32.h"
#undef JL_FEATURE_DEF
    -1);
static const auto real_feature_masks =
    feature_masks & FeatureList<feature_sz>{{UINT32_MAX, UINT32_MAX, 0}};

namespace Feature {
enum : uint32_t {
#define JL_FEATURE_DEF(name, bit, llvmver) name = bit,
#include "features_aarch32.h"
#undef JL_FEATURE_DEF
};
#undef JL_FEATURE_DEF_NAME
// This does not cover all dependencies (e.g. the ones that depends on arm versions)
static constexpr FeatureDep deps[] = {
    {neon, vfp3},
    {vfp4, vfp3},
    {crypto, neon},
};

// These are the real base requirements of the specific architectures
constexpr auto _armv7m = get_feature_masks(v7, mclass, hwdiv);
constexpr auto _armv7a = get_feature_masks(v7, aclass);
constexpr auto _armv7r = get_feature_masks(v7, rclass);
constexpr auto _armv8m = get_feature_masks(v7, v8, mclass, hwdiv);
constexpr auto _armv8a = get_feature_masks(v7, v8, aclass, neon, vfp3, vfp4, d32,
                                           hwdiv, hwdiv_arm);
constexpr auto _armv8r = get_feature_masks(v7, v8, rclass, neon, vfp3, vfp4, d32,
                                           hwdiv, hwdiv_arm);

// Set `generic` to match the feature requirement of the `C` code.
// we'll require at least these when compiling the sysimg.
#if __ARM_ARCH >= 8
#  if !defined(__ARM_ARCH_PROFILE)
constexpr auto generic = get_feature_masks(v7, v8, hwdiv);
#  elif __ARM_ARCH_PROFILE == 'A'
constexpr auto generic = _armv8a;
#  elif __ARM_ARCH_PROFILE == 'R'
constexpr auto generic = _armv8r;
#  elif __ARM_ARCH_PROFILE == 'M'
constexpr auto generic = _armv8m;
#  else
constexpr auto generic = get_feature_masks(v7, v8, hwdiv);
#  endif
#elif __ARM_ARCH == 7
#  if !defined(__ARM_ARCH_PROFILE)
constexpr auto generic = get_feature_masks(v7);
#  elif __ARM_ARCH_PROFILE == 'A'
constexpr auto generic = _armv7a;
#  elif __ARM_ARCH_PROFILE == 'R'
constexpr auto generic = _armv7r;
#  elif __ARM_ARCH_PROFILE == 'M'
constexpr auto generic = _armv7m;
#  else
constexpr auto generic = get_feature_masks(v7);
#  endif
#else
constexpr auto generic = get_feature_masks();
#endif

// All feature sets below should use or be or'ed with one of these (or generic).
// This makes sure that, for example, the `generic` target on `armv7-a` binary is equivalent
// to the `armv7-a` target.
constexpr auto armv7m = generic | _armv7m;
constexpr auto armv7a = generic | _armv7a;
constexpr auto armv7r = generic | _armv7r;
constexpr auto armv8m = generic | _armv8m;
constexpr auto armv8a = generic | _armv8a;
constexpr auto armv8r = generic | _armv8r;

// armv7l
constexpr auto arm_cortex_a5 = armv7a;
constexpr auto arm_cortex_a7 = armv7a | get_feature_masks(vfp3, vfp4, neon);
constexpr auto arm_cortex_a8 = armv7a | get_feature_masks(d32, vfp3, neon);
constexpr auto arm_cortex_a9 = armv7a;
constexpr auto arm_cortex_a12 = armv7a | get_feature_masks(d32, vfp3, vfp4, neon);
constexpr auto arm_cortex_a15 = armv7a | get_feature_masks(d32, vfp3, vfp4, neon);
constexpr auto arm_cortex_a17 = armv7a | get_feature_masks(d32, vfp3, vfp4, neon);
constexpr auto arm_cortex_r4 = armv7r | get_feature_masks(vfp3, hwdiv);
constexpr auto arm_cortex_r5 = armv7r | get_feature_masks(vfp3, hwdiv, hwdiv_arm);
constexpr auto arm_cortex_r7 = armv7r | get_feature_masks(vfp3, hwdiv, hwdiv_arm);
constexpr auto arm_cortex_r8 = armv7r | get_feature_masks(vfp3, hwdiv, hwdiv_arm);
constexpr auto qualcomm_scorpion = armv7a | get_feature_masks(v7, aclass, vfp3, neon);
constexpr auto qualcomm_krait = armv7a | get_feature_masks(vfp3, vfp4, neon, hwdiv, hwdiv_arm);
constexpr auto apple_swift = armv7a | get_feature_masks(d32, vfp3, vfp4, neon, hwdiv, hwdiv_arm);
constexpr auto marvell_pj4 = armv7a | get_feature_masks(vfp3);
constexpr auto intel_3735d = armv7a | get_feature_masks(vfp3, neon);
// armv8ml
constexpr auto arm_cortex_m23 = armv8m; // unsupported
constexpr auto arm_cortex_m33 = armv8m | get_feature_masks(v8_m_main); // unsupported
// armv8l
constexpr auto armv8a_crc = armv8a | get_feature_masks(crc);
constexpr auto armv8_1a = armv8a_crc | get_feature_masks(v8_1a);
constexpr auto armv8_2a = armv8_1a | get_feature_masks(v8_2a);
constexpr auto armv8a_crc_crypto = armv8a_crc | get_feature_masks(crypto);
constexpr auto armv8_2a_crypto = armv8_2a | get_feature_masks(crypto);
constexpr auto armv8_3a = armv8_2a | get_feature_masks(v8_3a);
constexpr auto armv8_3a_crypto = armv8_3a | get_feature_masks(crypto);
constexpr auto armv8_4a = armv8_3a | get_feature_masks(v8_4a);
constexpr auto armv8_4a_crypto = armv8_4a | get_feature_masks(crypto);
constexpr auto armv8_5a = armv8_4a | get_feature_masks(v8_5a);
constexpr auto armv8_5a_crypto = armv8_5a | get_feature_masks(crypto);
constexpr auto armv8_6a = armv8_5a | get_feature_masks(v8_6a);
constexpr auto armv8_6a_crypto = armv8_6a | get_feature_masks(crypto);

constexpr auto arm_cortex_a32 = armv8a_crc;
constexpr auto arm_cortex_r52 = armv8a_crc;
constexpr auto arm_cortex_a35 = armv8a_crc;
constexpr auto arm_cortex_a53 = armv8a_crc;
constexpr auto arm_cortex_a55 = armv8_2a;
constexpr auto arm_cortex_a57 = armv8a_crc;
constexpr auto arm_cortex_a72 = armv8a_crc;
constexpr auto arm_cortex_a73 = armv8a_crc;
constexpr auto arm_cortex_a75 = armv8_2a;
constexpr auto arm_cortex_a76 = armv8_2a;
constexpr auto arm_cortex_a77 = armv8_2a;
constexpr auto arm_cortex_a78 = armv8_2a;
constexpr auto arm_cortex_x1 = armv8_2a;
constexpr auto arm_neoverse_n1 = armv8_2a;
constexpr auto arm_neoverse_v1 = armv8_4a;
constexpr auto arm_neoverse_n2 = armv8_5a;
constexpr auto nvidia_denver1 = armv8a; // TODO? (crc, crypto)
constexpr auto nvidia_denver2 = armv8a_crc_crypto;
constexpr auto apm_xgene1 = armv8a;
constexpr auto apm_xgene2 = armv8a; // TODO?
constexpr auto apm_xgene3 = armv8a; // TODO?
constexpr auto qualcomm_kyro = armv8a_crc_crypto;
constexpr auto qualcomm_falkor = armv8a_crc_crypto;
constexpr auto qualcomm_saphira = armv8_3a_crypto;
constexpr auto samsung_exynos_m1 = armv8a_crc_crypto;
constexpr auto samsung_exynos_m2 = armv8a_crc_crypto;
constexpr auto samsung_exynos_m3 = armv8a_crc_crypto;
constexpr auto samsung_exynos_m4 = armv8_2a_crypto;
constexpr auto samsung_exynos_m5 = samsung_exynos_m4;
constexpr auto apple_a7 = armv8a_crc_crypto;

}

static constexpr CPUSpec<CPU, feature_sz> cpus[] = {
    {"generic", CPU::generic, CPU::generic, 0, Feature::generic},
    // armv6
    {"mpcore", CPU::arm_mpcore, CPU::generic, 0, Feature::generic},
    {"arm1136jf-s", CPU::arm_1136jf_s, CPU::generic, 0, Feature::generic},
    {"arm1156t2f-s", CPU::arm_1156t2f_s, CPU::generic, 0, Feature::generic},
    {"arm1176jzf-s", CPU::arm_1176jzf_s, CPU::generic, 0, Feature::generic},
    {"cortex-m0", CPU::arm_cortex_m0, CPU::generic, 0, Feature::generic},
    {"cortex-m1", CPU::arm_cortex_m1, CPU::generic, 0, Feature::generic},
    // armv7ml
    {"armv7-m", CPU::armv7_m, CPU::generic, 0, Feature::armv7m},
    {"armv7e-m", CPU::armv7e_m, CPU::generic, 0, Feature::armv7m},
    {"cortex-m3", CPU::arm_cortex_m3, CPU::generic, 0, Feature::armv7m},
    {"cortex-m4", CPU::arm_cortex_m4, CPU::generic, 0, Feature::armv7m},
    {"cortex-m7", CPU::arm_cortex_m7, CPU::generic, 0, Feature::armv7m},
    // armv7l
    {"armv7-a", CPU::armv7_a, CPU::generic, 0, Feature::armv7a},
    {"armv7-r", CPU::armv7_r, CPU::generic, 0, Feature::armv7r},
    {"cortex-a5", CPU::arm_cortex_a5, CPU::generic, 0, Feature::arm_cortex_a5},
    {"cortex-a7", CPU::arm_cortex_a7, CPU::generic, 0, Feature::arm_cortex_a7},
    {"cortex-a8", CPU::arm_cortex_a8, CPU::generic, 0, Feature::arm_cortex_a8},
    {"cortex-a9", CPU::arm_cortex_a9, CPU::generic, 0, Feature::arm_cortex_a9},
    {"cortex-a12", CPU::arm_cortex_a12, CPU::generic, 0, Feature::arm_cortex_a12},
    {"cortex-a15", CPU::arm_cortex_a15, CPU::generic, 0, Feature::arm_cortex_a15},
    {"cortex-a17", CPU::arm_cortex_a17, CPU::generic, 0, Feature::arm_cortex_a17},
    {"cortex-r4", CPU::arm_cortex_r4, CPU::generic, 0, Feature::arm_cortex_r4},
    {"cortex-r5", CPU::arm_cortex_r5, CPU::generic, 0, Feature::arm_cortex_r5},
    {"cortex-r7", CPU::arm_cortex_r7, CPU::generic, 0, Feature::arm_cortex_r7},
    {"cortex-r8", CPU::arm_cortex_r8, CPU::generic, 0, Feature::arm_cortex_r8},
    {"scorpion", CPU::qualcomm_scorpion, CPU::armv7_a, UINT32_MAX, Feature::qualcomm_scorpion},
    {"krait", CPU::qualcomm_krait, CPU::generic, 0, Feature::qualcomm_krait},
    {"swift", CPU::apple_swift, CPU::generic, 0, Feature::apple_swift},
    {"pj4", CPU::marvell_pj4, CPU::armv7_a, UINT32_MAX, Feature::marvell_pj4},
    {"3735d", CPU::intel_3735d, CPU::armv7_a, UINT32_MAX, Feature::intel_3735d},

    // armv8ml
    {"armv8-m.base", CPU::armv8_m_base, CPU::generic, 0, Feature::armv8m},
    {"armv8-m.main", CPU::armv8_m_main, CPU::generic, 0, Feature::armv8m},
    {"cortex-m23", CPU::arm_cortex_m23, CPU::armv8_m_base, 0, Feature::arm_cortex_m23},
    {"cortex-m33", CPU::arm_cortex_m33, CPU::armv8_m_main, 0, Feature::arm_cortex_m33},

    // armv8l
    {"armv8-a", CPU::armv8_a, CPU::generic, 0, Feature::armv8a},
    {"armv8-r", CPU::armv8_r, CPU::generic, 0, Feature::armv8r},
    {"armv8.1-a", CPU::armv8_1_a, CPU::generic, 0, Feature::armv8_1a},
    {"armv8.2-a", CPU::armv8_2_a, CPU::generic, 0, Feature::armv8_2a},
    {"armv8.3-a", CPU::armv8_3_a, CPU::generic, 0, Feature::armv8_3a},
    {"armv8.4-a", CPU::armv8_4_a, CPU::generic, 0, Feature::armv8_4a},
    {"armv8.5-a", CPU::armv8_5_a, CPU::generic, 0, Feature::armv8_5a},
    {"armv8.6_a", CPU::armv8_6_a, CPU::generic, 0, Feature::armv8_6a},
    {"cortex-a32", CPU::arm_cortex_a32, CPU::generic, 0, Feature::arm_cortex_a32},
    {"cortex-r52", CPU::arm_cortex_r52, CPU::generic, 0, Feature::arm_cortex_r52},
    {"cortex-a35", CPU::arm_cortex_a35, CPU::generic, 0, Feature::arm_cortex_a35},
    {"cortex-a53", CPU::arm_cortex_a53, CPU::generic, 0, Feature::arm_cortex_a53},
    {"cortex-a55", CPU::arm_cortex_a55, CPU::generic, 0, Feature::arm_cortex_a55},
    {"cortex-a57", CPU::arm_cortex_a57, CPU::generic, 0, Feature::arm_cortex_a57},
    {"cortex-a72", CPU::arm_cortex_a72, CPU::generic, 0, Feature::arm_cortex_a72},
    {"cortex-a73", CPU::arm_cortex_a73, CPU::generic, 0, Feature::arm_cortex_a73},
    {"cortex-a75", CPU::arm_cortex_a75, CPU::generic, 0, Feature::arm_cortex_a75},
    {"cortex-a76", CPU::arm_cortex_a76, CPU::generic, 0, Feature::arm_cortex_a76},
    {"cortex-a76ae", CPU::arm_cortex_a76ae, CPU::generic, 0, Feature::arm_cortex_a76},
    {"cortex-a77", CPU::arm_cortex_a77, CPU::arm_cortex_a76, 110000, Feature::arm_cortex_a77},
    {"cortex-a78", CPU::arm_cortex_a78, CPU::arm_cortex_a77, 110000, Feature::arm_cortex_a78},
    {"cortex-x1", CPU::arm_cortex_x1, CPU::arm_cortex_a78, 110000, Feature::arm_cortex_x1},
    {"neoverse-n1", CPU::arm_neoverse_n1, CPU::arm_cortex_a76, 100000, Feature::arm_neoverse_n1},
    {"neoverse-v1", CPU::arm_neoverse_v1, CPU::arm_neoverse_n1, UINT32_MAX, Feature::arm_neoverse_v1},
    {"neoverse-n2", CPU::arm_neoverse_n2, CPU::arm_neoverse_n1, UINT32_MAX, Feature::arm_neoverse_n2},
    {"denver1", CPU::nvidia_denver1, CPU::arm_cortex_a53, UINT32_MAX, Feature::nvidia_denver1},
    {"denver2", CPU::nvidia_denver2, CPU::arm_cortex_a57, UINT32_MAX, Feature::nvidia_denver2},
    {"xgene1", CPU::apm_xgene1, CPU::armv8_a, UINT32_MAX, Feature::apm_xgene1},
    {"xgene2", CPU::apm_xgene2, CPU::armv8_a, UINT32_MAX, Feature::apm_xgene2},
    {"xgene3", CPU::apm_xgene3, CPU::armv8_a, UINT32_MAX, Feature::apm_xgene3},
    {"kyro", CPU::qualcomm_kyro, CPU::armv8_a, UINT32_MAX, Feature::qualcomm_kyro},
    {"falkor", CPU::qualcomm_falkor, CPU::armv8_a, UINT32_MAX, Feature::qualcomm_falkor},
    {"saphira", CPU::qualcomm_saphira, CPU::armv8_a, UINT32_MAX, Feature::qualcomm_saphira},
    {"exynos-m1", CPU::samsung_exynos_m1, CPU::generic, UINT32_MAX, Feature::samsung_exynos_m1},
    {"exynos-m2", CPU::samsung_exynos_m2, CPU::generic, UINT32_MAX, Feature::samsung_exynos_m2},
    {"exynos-m3", CPU::samsung_exynos_m3, CPU::generic, 0, Feature::samsung_exynos_m3},
    {"exynos-m4", CPU::samsung_exynos_m4, CPU::generic, 0, Feature::samsung_exynos_m4},
    {"exynos-m5", CPU::samsung_exynos_m5, CPU::samsung_exynos_m4, 110000, Feature::samsung_exynos_m5},
    {"apple-a7", CPU::apple_a7, CPU::generic, 0, Feature::apple_a7},
};
#endif
static constexpr size_t ncpu_names = sizeof(cpus) / sizeof(cpus[0]);

static inline const CPUSpec<CPU,feature_sz> *find_cpu(uint32_t cpu)
{
    return ::find_cpu(cpu, cpus, ncpu_names);
}

static inline const CPUSpec<CPU,feature_sz> *find_cpu(llvm::StringRef name)
{
    return ::find_cpu(name, cpus, ncpu_names);
}

static inline const char *find_cpu_name(uint32_t cpu)
{
    return ::find_cpu_name(cpu, cpus, ncpu_names);
}

#if defined _CPU_AARCH64_ && defined _OS_DARWIN_

static NOINLINE std::pair<uint32_t,FeatureList<feature_sz>> _get_host_cpu()
{
    using namespace llvm;
    char buffer[128];
    size_t bufferlen = 128;
    sysctlbyname("machdep.cpu.brand_string",&buffer,&bufferlen,NULL,0);
    StringRef cpu_name(buffer);
    if (cpu_name.find("M1") != StringRef ::npos)
        return std::make_pair((uint32_t)CPU::apple_m1, Feature::apple_m1);
    else if (cpu_name.find("M2") != StringRef ::npos)
        return std::make_pair((uint32_t)CPU::apple_m2, Feature::apple_m2);
    else if (cpu_name.find("M3") != StringRef ::npos)
        return std::make_pair((uint32_t)CPU::apple_m3, Feature::apple_m3);
    else if (cpu_name.find("M4") != StringRef ::npos)
        return std::make_pair((uint32_t)CPU::apple_m4, Feature::apple_m4);
    else
        return std::make_pair((uint32_t)CPU::apple_m1, Feature::apple_m1);
}

#else

// auxval reader

#ifndef AT_HWCAP
#  define AT_HWCAP 16
#endif
#ifndef AT_HWCAP2
#  define AT_HWCAP2 26
#endif

#if defined(_OS_FREEBSD_)
static inline unsigned long jl_getauxval(unsigned long type)
{
    unsigned long val;
    if (elf_aux_info((int)type, &val, sizeof(val)) != 0) {
        return 0;
    }
    return val;
}
#elif defined(USE_DYN_GETAUXVAL)
static unsigned long getauxval_procfs(unsigned long type)
{
    int fd = open("/proc/self/auxv", O_RDONLY);
    if (fd == -1)
        return 0;
    unsigned long val = 0;
    unsigned long buff[2];
    while (read(fd, buff, sizeof(buff)) == sizeof(buff)) {
        if (buff[0] == 0)
            break;
        if (buff[0] == type) {
            val = buff[1];
            break;
        }
    }
    close(fd);
    return val;
}

static inline unsigned long jl_getauxval(unsigned long type)
{
    // First, try resolving getauxval in libc
    auto libc = jl_dlopen(nullptr, JL_RTLD_LOCAL);
    static unsigned long (*getauxval_p)(unsigned long) = NULL;
    if (getauxval_p == NULL && jl_dlsym(libc, "getauxval", (void **)&getauxval_p, 0)) {
        return getauxval_p(type);
    }

    // If we couldn't resolve it, use procfs.
    return getauxval_procfs(type);
}
#else
static inline unsigned long jl_getauxval(unsigned long type)
{
    return getauxval(type);
}
#endif

struct CPUID {
    uint8_t implementer;
    uint8_t variant;
    uint16_t part;
    bool operator<(const CPUID &right) const
    {
        if (implementer < right.implementer)
            return true;
        if (implementer > right.implementer)
            return false;
        if (part < right.part)
            return true;
        if (part > right.part)
            return false;
        return variant < right.variant;
    }
};

// /sys/devices/system/cpu/cpu<n>/regs/identification/midr_el1 reader
static inline void get_cpuinfo_sysfs(std::set<CPUID> &res)
{
    // This only works on a 64bit 4.7+ kernel
    auto dir = opendir("/sys/devices/system/cpu");
    if (!dir)
        return;
    while (auto entry = readdir(dir)) {
        if (entry->d_type != DT_DIR)
            continue;
        if (strncmp(entry->d_name, "cpu", 3) != 0)
            continue;
        std::string stm;
        llvm::raw_string_ostream(stm) << "/sys/devices/system/cpu/" << entry->d_name << "/regs/identification/midr_el1";
        std::ifstream file(stm);
        if (!file)
            continue;
        uint64_t val = 0;
        file >> std::hex >> val;
        if (!file)
            continue;
        CPUID cpuid = {
            uint8_t(val >> 24),
            uint8_t((val >> 20) & 0xf),
            uint16_t((val >> 4) & 0xfff)
        };
        res.insert(cpuid);
    }
    closedir(dir);
}

// Use an external template since lambda's can't be templated in C++11
template<typename T, typename F>
static inline bool try_read_procfs_line(llvm::StringRef line, const char *prefix, T &out,
                                        bool &flag, F &&reset)
{
    if (!line.starts_with(prefix))
        return false;
    if (flag)
        reset();
    flag = line.substr(strlen(prefix)).ltrim("\t :").getAsInteger(0, out);
    return true;
}

// /proc/cpuinfo reader
static inline void get_cpuinfo_procfs(std::set<CPUID> &res)
{
    std::ifstream file("/proc/cpuinfo");
    CPUID cpuid = {0, 0, 0};
    bool impl = false;
    bool part = false;
    bool var = false;
    auto reset = [&] () {
        if (impl && part)
            res.insert(cpuid);
        impl = false;
        part = false;
        var = false;
        memset(&cpuid, 0, sizeof(cpuid));
    };
    for (std::string line; std::getline(file, line);) {
        if (line.empty()) {
            reset();
            continue;
        }
        try_read_procfs_line(line, "CPU implementer", cpuid.implementer, impl, reset) ||
            try_read_procfs_line(line, "CPU variant", cpuid.variant, var, reset) ||
            try_read_procfs_line(line, "CPU part", cpuid.part, part, reset);
    }
    reset();
}

static std::set<CPUID> get_cpuinfo(void)
{
    std::set<CPUID> res;
    get_cpuinfo_sysfs(res);
    if (res.empty())
        get_cpuinfo_procfs(res);
    return res;
}

static CPU get_cpu_name(CPUID cpuid)
{
    switch (cpuid.implementer) {
    case 0x41: // 'A': ARM
        switch (cpuid.part) {
        case 0xb02: return CPU::arm_mpcore;
        case 0xb36: return CPU::arm_1136jf_s;
        case 0xb56: return CPU::arm_1156t2f_s;
        case 0xb76: return CPU::arm_1176jzf_s;
        case 0xc05: return CPU::arm_cortex_a5;
        case 0xc07: return CPU::arm_cortex_a7;
        case 0xc08: return CPU::arm_cortex_a8;
        case 0xc09: return CPU::arm_cortex_a9;
        case 0xc0d: return CPU::arm_cortex_a12;
        case 0xc0f: return CPU::arm_cortex_a15;
        case 0xc0e: return CPU::arm_cortex_a17;
        case 0xc14: return CPU::arm_cortex_r4;
        case 0xc15: return CPU::arm_cortex_r5;
        case 0xc17: return CPU::arm_cortex_r7;
        case 0xc18: return CPU::arm_cortex_r8;
        case 0xc20: return CPU::arm_cortex_m0;
        case 0xc21: return CPU::arm_cortex_m1;
        case 0xc23: return CPU::arm_cortex_m3;
        case 0xc24: return CPU::arm_cortex_m4;
        case 0xc27: return CPU::arm_cortex_m7;
        case 0xd01: return CPU::arm_cortex_a32;
        case 0xd02: return CPU::arm_cortex_a34;
        case 0xd03: return CPU::arm_cortex_a53;
        case 0xd04: return CPU::arm_cortex_a35;
        case 0xd05: return CPU::arm_cortex_a55;
        case 0xd06: return CPU::arm_cortex_a65;
        case 0xd07: return CPU::arm_cortex_a57;
        case 0xd08: return CPU::arm_cortex_a72;
        case 0xd09: return CPU::arm_cortex_a73;
        case 0xd0a: return CPU::arm_cortex_a75;
        case 0xd0b: return CPU::arm_cortex_a76;
        case 0xd0c: return CPU::arm_neoverse_n1;
        case 0xd0d: return CPU::arm_cortex_a77;
        case 0xd0e: return CPU::arm_cortex_a76ae;
        case 0xd13: return CPU::arm_cortex_r52;
        case 0xd20: return CPU::arm_cortex_m23;
        case 0xd21: return CPU::arm_cortex_m33;
            // case 0xd22: return CPU::arm_cortex_m55;
        case 0xd40: return CPU::arm_neoverse_v1;
        case 0xd41: return CPU::arm_cortex_a78;
        case 0xd43: return CPU::arm_cortex_a65ae;
        case 0xd44: return CPU::arm_cortex_x1;
        case 0xd49: return CPU::arm_neoverse_n2;
        case 0xd4a: return CPU::arm_neoverse_e1;
        default: return CPU::generic;
        }
    case 0x42: // 'B': Broadcom (Cavium)
        switch (cpuid.part) {
            // case 0x00f: return CPU::broadcom_brahma_b15;
            // case 0x100: return CPU::broadcom_brahma_b53;
        case 0x516: return CPU::cavium_thunderx2t99p1;
        default: return CPU::generic;
        }
    case 0x43: // 'C': Cavium
        switch (cpuid.part) {
        case 0xa0: return CPU::cavium_thunderx;
        case 0xa1:
            if (cpuid.variant == 0)
                return CPU::cavium_thunderx88p1;
            return CPU::cavium_thunderx88;
        case 0xa2: return CPU::cavium_thunderx81;
        case 0xa3: return CPU::cavium_thunderx83;
        case 0xaf: return CPU::cavium_thunderx2t99;
        case 0xb0: return CPU::cavium_octeontx2;
        case 0xb1: return CPU::cavium_octeontx2t98;
        case 0xb2: return CPU::cavium_octeontx2t96;
        case 0xb3: return CPU::cavium_octeontx2f95;
        case 0xb4: return CPU::cavium_octeontx2f95n;
        case 0xb5: return CPU::cavium_octeontx2f95mm;
        case 0xb8: return CPU::marvell_thunderx3t110;
        default: return CPU::generic;
        }
    case 0x46: // 'F': Fujitsu
        switch (cpuid.part) {
        case 0x1: return CPU::fujitsu_a64fx;
        default: return CPU::generic;
        }
    case 0x48: // 'H': HiSilicon
        switch (cpuid.part) {
        case 0xd01: return CPU::hisilicon_tsv110;
        case 0xd40: return CPU::arm_cortex_a76; // Kirin 980
        default: return CPU::generic;
        }
    case 0x4e: // 'N': NVIDIA
        switch (cpuid.part) {
        case 0x000: return CPU::nvidia_denver1;
        case 0x003: return CPU::nvidia_denver2;
        case 0x004: return CPU::nvidia_carmel;
        default: return CPU::generic;
        }
    case 0x50: // 'P': AppliedMicro
        // x-gene 2
        // x-gene 3
        switch (cpuid.part) {
        case 0x000: return CPU::apm_xgene1;
        default: return CPU::generic;
        }
    case 0x51: // 'Q': Qualcomm
        switch (cpuid.part) {
        case 0x00f:
        case 0x02d:
            return CPU::qualcomm_scorpion;
        case 0x04d:
        case 0x06f:
            return CPU::qualcomm_krait;
        case 0x201: // silver
        case 0x205: // gold
        case 0x211: // silver
            return CPU::qualcomm_kyro;
            // kryo 2xx
        case 0x800: // gold
            return CPU::arm_cortex_a73;
        case 0x801: // silver
            return CPU::arm_cortex_a53;
            // kryo 3xx
        case 0x802: // gold
            return CPU::arm_cortex_a75;
        case 0x803: // silver
            return CPU::arm_cortex_a55;
            // kryo 4xx
        case 0x804: // gold
            return CPU::arm_cortex_a76;
        case 0x805: // silver
            return CPU::arm_cortex_a55;
            // kryo 5xx seems to be using ID for cortex-a77 directly
        case 0xc00:
            return CPU::qualcomm_falkor;
        case 0xc01:
            return CPU::qualcomm_saphira;
        default: return CPU::generic;
        }
    case 0x53: // 'S': Samsung
        if (cpuid.part == 1) {
            if (cpuid.variant == 4)
                return CPU::samsung_exynos_m2;
            return CPU::samsung_exynos_m1;
        }
        if (cpuid.variant != 1)
            return CPU::generic;
        switch (cpuid.part) {
        case 0x2: return CPU::samsung_exynos_m3;
        case 0x3: return CPU::samsung_exynos_m4;
        case 0x4: return CPU::samsung_exynos_m5;
        default: return CPU::generic;
        }
    case 0x56: // 'V': Marvell
        switch (cpuid.part) {
        case 0x581:
        case 0x584:
            return CPU::marvell_pj4;
        default: return CPU::generic;
        }
    case 0x61: // 'a': Apple
        // Data here is partially based on these sources:
        // https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/arm/cpuid.h
        // https://asahilinux.org/docs/hw/soc/soc-codenames/#socs
        // https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64Processors.td
        switch (cpuid.part) {
        case 0x0: // Swift
            return CPU::apple_swift;
        case 0x1: // Cyclone
            return CPU::apple_a7;
        case 0x2: // Typhoon
        case 0x3: // Typhoo/Capri
            return CPU::apple_a8;
        case 0x4: // Twister
        case 0x5: // Twister/Elba/Malta
            return CPU::apple_a9;
        case 0x6: // Hurricane
        case 0x7: // Hurricane/Myst
            return CPU::apple_a10;
        case 0x8: // Monsoon
        case 0x9: // Mistral
            return CPU::apple_a11;
        case 0xB: // Vortex
        case 0xC: // Tempest
        case 0x10: // A12X, Vortex Aruba
        case 0x11: // A12X, Tempest Aruba
            return CPU::apple_a12;
        case 0xF: // Tempest M9
            return CPU::apple_s4;
        case 0x12: // H12 Cebu p-Core "Lightning"
        case 0x13: // H12 Cebu e-Core "Thunder"
            return CPU::apple_a13;
        case 0x20: // H13 Sicily e-Core "Icestorm"
        case 0x21: // H13 Sicily p-Core "Firestorm"
            return CPU::apple_a14;
        case 0x22: // H13G Tonga e-Core "Icestorm" used in Apple M1
        case 0x23: // H13G Tonga p-Core "Firestorm" used in Apple M1
        case 0x24: // H13J Jade Chop e-Core "Icestorm" used in Apple M1 Pro
        case 0x25: // H13J Jade Chop p-Core "Firestorm" used in Apple M1 Pro
        case 0x28: // H13J Jade Die e-Core "Icestorm" used in Apple M1 Max / Ultra
        case 0x29: // H13J Jade Die p-Core "Firestorm" used in Apple M1 Max / Ultra
            return CPU::apple_m1;
        case 0x30: // H14 Ellis e-Core "Blizzard" used in Apple A15
        case 0x31: // H14 Ellis p-Core "Avalanche" used in Apple A15
            return CPU::apple_a15;
        case 0x32: // H14G Staten e-Core "Blizzard" used in Apple M2
        case 0x33: // H14G Staten p-Core "Avalanche" used in Apple M2
        case 0x34: // H14S Rhodes Chop e-Core "Blizzard" used in Apple M2 Pro
        case 0x35: // H14S Rhodes Chop p-Core "Avalanche" used in Apple M2 Pro
        case 0x38: // H14C Rhodes Die e-Core "Blizzard" used in Apple M2 Max / Ultra
        case 0x39: // H14C Rhodes Die p-Core "Avalanche" used in Apple M2 Max / Ultra
            return CPU::apple_m2;
        case 0x40: // H15 Crete e-Core "Sawtooth" used in Apple A16
        case 0x41: // H15 Crete p-Core "Everest" used in Apple A16
            return CPU::apple_a16;
        case 0x42: // H15 Ibiza e-Core "Sawtooth" used in Apple M3
        case 0x43: // H15 Ibiza p-Core "Everest" used in Apple M3
        case 0x44: // H15 Lobos e-Core "Sawtooth" used in Apple M3 Pro
        case 0x45: // H15 Lobos p-Core "Everest" used in Apple M3 Pro
        case 0x49: // H15 Palma e-Core "Sawtooth" used in Apple M3 Max
        case 0x48: // H15 Palma p-Core "Everest" used in Apple M3 Max
            return CPU::apple_m3;
        //case 0x46: // M11 e-Core "Sawtooth" used in Apple S9
        //case 0x47:  does not exist
            //return CPU::apple_s9;
        case 0x50: // H15 Coll e-Core "Sawtooth" used in Apple A17 Pro
        case 0x51: // H15 Coll p-Core "Everest" used in Apple A17 Pro
            return CPU::apple_a17;
        case 0x52: // H16G Donan e-Core used in Apple M4
        case 0x53: // H16H Donan p-Core used in Apple M4
        case 0x54: // H16S Brava S e-Core used in Apple M4 Pro
        case 0x55: // H16S Brava S p-Core used in Apple M4 Pro
        case 0x58: // H16C Brava C e-Core used in Apple M4 Max
        case 0x59: // H16C Brava C p-Core used in Apple M4 Max
            return CPU::apple_m4;
        //case 0x60: // H17P Tahiti e-Core used in Apple A18 Pro
        //case 0x61: // H17P Tahiti p-Core used in Apple A18 Pro
        //case 0x6a: // H17A Tupai e-Core used in Apple A18
        //case 0x6b: // H17A Tupai p-Core used in Apple A18
            //return CPU::apple_a18;
        default: return CPU::generic;
        }
    case 0x68: // 'h': Huaxintong Semiconductor
        switch (cpuid.part) {
        case 0x0: return CPU::hxt_phecda;
        default: return CPU::generic;
        }
    case 0x69: // 'i': Intel
        switch (cpuid.part) {
        case 0x001: return CPU::intel_3735d;
        default: return CPU::generic;
        }
    default:
        return CPU::generic;
    }
}




namespace {

struct arm_arch {
    int version;
    char klass;
    constexpr bool mclass() const { return klass == 'M'; }
};

}

static arm_arch get_elf_arch(void)
{
#ifdef _CPU_AARCH64_
    return {8, 'A'};
#else
    int ver = 0;
    char profile = 0;
    struct utsname name;
    if (uname(&name) >= 0) {
        // name.machine is the elf_platform in the kernel.
        if (strcmp(name.machine, "armv6l") == 0) {
            ver = 6;
        }
        else if (strcmp(name.machine, "armv7l") == 0) {
            ver = 7;
        }
        else if (strcmp(name.machine, "armv7ml") == 0) {
            ver = 7;
            profile = 'M';
        }
        else if (strcmp(name.machine, "armv8l") == 0 || strcmp(name.machine, "aarch64") == 0) {
            ver = 8;
        }
    }
    if (__ARM_ARCH > ver)
        ver = __ARM_ARCH;
#  if __ARM_ARCH > 6 && defined(__ARM_ARCH_PROFILE)
    profile = __ARM_ARCH_PROFILE;
#  endif
    return {ver, profile};
#endif
}

static arm_arch feature_arch_version(const FeatureList<feature_sz> &feature)
{
#ifdef _CPU_AARCH64_
    return {8, 'A'};
#else
    int ver;
    if (test_nbit(feature, Feature::v8)) {
        ver = 8;
    }
    else if (test_nbit(feature, Feature::v7)) {
        ver = 7;
    }
    else {
        return {6, 0};
    }
    if (test_nbit(feature, Feature::mclass)) {
        return {ver, 'M'};
    }
    else if (test_nbit(feature, Feature::rclass)) {
        return {ver, 'R'};
    }
    else if (test_nbit(feature, Feature::aclass)) {
        return {ver, 'A'};
    }
    return {ver, 0};
#endif
}

static CPU generic_for_arch(arm_arch arch)
{
#ifdef _CPU_AARCH64_
    return CPU::generic;
#else
#  if defined(__ARM_ARCH_PROFILE)
    char klass = __ARM_ARCH_PROFILE;
#  else
    char klass = arch.klass;
#  endif
    if (arch.version >= 8) {
        if (klass == 'M') {
            return CPU::armv8_m_base;
        }
        else if (klass == 'R') {
            return CPU::armv8_r;
        }
        else {
            return CPU::armv8_a;
        }
    }
    else if (arch.version == 7) {
        if (klass == 'M') {
            return CPU::armv7_m;
        }
        else if (klass == 'R') {
            return CPU::armv7_r;
        }
        else {
            return CPU::armv7_a;
        }
    }
    return CPU::generic;
#endif
}

static bool check_cpu_arch_ver(uint32_t cpu, arm_arch arch)
{
    auto spec = find_cpu(cpu);
    // This happens on AArch64 and indicates that the cpu name isn't a valid aarch64 CPU
    if (!spec)
        return false;
    auto feature_arch = feature_arch_version(spec->features);
    if (arch.mclass() != feature_arch.mclass())
        return false;
    if (arch.version > feature_arch.version)
        return false;
    return true;
}

static void shrink_big_little(llvm::SmallVectorImpl<std::pair<uint32_t,CPUID>> &list,
                              const CPU *cpus, uint32_t ncpu)
{
    auto find = [&] (uint32_t name) {
        for (uint32_t i = 0; i < ncpu; i++) {
            if (cpus[i] == CPU(name)) {
                return (int)i;
            }
        }
        return -1;
    };
    int maxidx = -1;
    for (auto &ele: list) {
        int idx = find(ele.first);
        if (idx > maxidx) {
            maxidx = idx;
        }
    }
    if (maxidx >= 0) {
        list.erase(std::remove_if(list.begin(), list.end(), [&] (std::pair<uint32_t,CPUID> &ele) {
                    int idx = find(ele.first);
                    return idx != -1 && idx < maxidx;
                }), list.end());
    }
}

static NOINLINE std::pair<uint32_t,FeatureList<feature_sz>> _get_host_cpu()
{
    FeatureList<feature_sz> features = {};
    // Here we assume that only the lower 32bit are used on aarch64
    // Change the cast here when that's not the case anymore (and when there's features in the
    // high bits that we want to detect).
    features[0] = (uint32_t)jl_getauxval(AT_HWCAP);
    features[1] = (uint32_t)jl_getauxval(AT_HWCAP2);
#ifdef _CPU_AARCH64_
    if (test_nbit(features, 31)) // HWCAP_PACG
        set_bit(features, Feature::pauth, true);
#endif
    auto cpuinfo = get_cpuinfo();
    auto arch = get_elf_arch();
#ifdef _CPU_ARM_
    if (arch.version >= 7) {
        if (arch.klass == 'M') {
            set_bit(features, Feature::mclass, true);
        }
        else if (arch.klass == 'R') {
            set_bit(features, Feature::rclass, true);
        }
        else if (arch.klass == 'A') {
            set_bit(features, Feature::aclass, true);
        }
    }
    switch (arch.version) {
    case 8:
        set_bit(features, Feature::v8, true);
        JL_FALLTHROUGH;
    case 7:
        set_bit(features, Feature::v7, true);
        break;
    default:
        break;
    }
#endif

    std::set<uint32_t> cpus;
    llvm::SmallVector<std::pair<uint32_t,CPUID>, 0> list;
    // Ideally the feature detection above should be enough.
    // However depending on the kernel version not all features are available
    // and it's also impossible to detect the ISA version which contains
    // some features not yet exposed by the kernel.
    // We therefore try to get a more complete feature list from the CPU name.
    // Since it is possible to pair cores that have different feature set
    // (Observed for exynos 9810 with exynos-m3 + cortex-a55) we'll compute
    // an intersection of the known features from each core.
    // If there's a core that we don't recognize, treat it as generic.
    bool extra_initialized = false;
    FeatureList<feature_sz> extra_features = {};
    for (auto info: cpuinfo) {
        auto name = (uint32_t)get_cpu_name(info);
        if (name == 0) {
            // no need to clear the feature set if it wasn't initialized
            if (extra_initialized)
                extra_features = FeatureList<feature_sz>{};
            extra_initialized = true;
            continue;
        }
        if (!check_cpu_arch_ver(name, arch))
            continue;
        if (cpus.insert(name).second) {
            if (extra_initialized) {
                extra_features = extra_features & find_cpu(name)->features;
            }
            else {
                extra_initialized = true;
                extra_features = find_cpu(name)->features;
            }
            list.emplace_back(name, info);
        }
    }
    features = features | extra_features;

    // Not all elements/pairs are valid
    static constexpr CPU v8order[] = {
        CPU::arm_cortex_a35,
        CPU::arm_cortex_a53,
        CPU::arm_cortex_a55,
        CPU::arm_cortex_a57,
        CPU::arm_cortex_a72,
        CPU::arm_cortex_a73,
        CPU::arm_cortex_a75,
        CPU::arm_cortex_a76,
        CPU::arm_neoverse_n1,
        CPU::arm_neoverse_n2,
        CPU::arm_neoverse_v1,
        CPU::nvidia_denver2,
        CPU::nvidia_carmel,
        CPU::samsung_exynos_m1,
        CPU::samsung_exynos_m2,
        CPU::samsung_exynos_m3,
        CPU::samsung_exynos_m4,
        CPU::samsung_exynos_m5,
    };
    shrink_big_little(list, v8order, sizeof(v8order) / sizeof(CPU));
#ifdef _CPU_ARM_
    // Not all elements/pairs are valid
    static constexpr CPU v7order[] = {
        CPU::arm_cortex_a5,
        CPU::arm_cortex_a7,
        CPU::arm_cortex_a8,
        CPU::arm_cortex_a9,
        CPU::arm_cortex_a12,
        CPU::arm_cortex_a15,
        CPU::arm_cortex_a17
    };
    shrink_big_little(list, v7order, sizeof(v7order) / sizeof(CPU));
#endif
    uint32_t cpu = 0;
    if (list.empty()) {
        cpu = (uint32_t)generic_for_arch(arch);
    }
    else {
        // This also covers `list.size() > 1` case which means there's a unknown combination
        // consists of CPU's we know. Unclear what else we could try so just randomly return
        // one...
        cpu = list[0].first;
    }
    // Ignore feature bits that we are not interested in.
    mask_features(feature_masks, &features[0]);
    return std::make_pair(cpu, features);
}
#endif

static inline const std::pair<uint32_t,FeatureList<feature_sz>> &get_host_cpu()
{
    static auto host_cpu = _get_host_cpu();
    return host_cpu;
}

static bool is_generic_cpu_name(uint32_t cpu)
{
    switch ((CPU)cpu) {
    case CPU::generic:
    case CPU::armv7_a:
    case CPU::armv7_m:
    case CPU::armv7e_m:
    case CPU::armv7_r:
    case CPU::armv8_a:
    case CPU::armv8_m_base:
    case CPU::armv8_m_main:
    case CPU::armv8_r:
    case CPU::armv8_1_a:
    case CPU::armv8_2_a:
    case CPU::armv8_3_a:
    case CPU::armv8_4_a:
    case CPU::armv8_5_a:
    case CPU::armv8_6_a:
        return true;
    default:
        return false;
    }
}

static inline const std::string &host_cpu_name()
{
    static std::string name = [] {
        if (is_generic_cpu_name(get_host_cpu().first)) {
            auto llvm_name = jl_get_cpu_name_llvm();
            if (llvm_name != "generic") {
                return llvm_name;
            }
        }
        return std::string(find_cpu_name(get_host_cpu().first));
    }();
    return name;
}

static inline const char *normalize_cpu_name(llvm::StringRef name)
{
    if (name == "ares")
        return "neoverse-n1";
    if (name == "zeus")
        return "neoverse-v1";
    if (name == "cyclone")
        return "apple-a7";
    if (name == "typhoon")
        return "apple-a8";
    if (name == "twister")
        return "apple-a9";
    if (name == "hurricane")
        return "apple-a10";
    return nullptr;
}

template<size_t n>
static inline void enable_depends(FeatureList<n> &features)
{
    if (test_nbit(features, Feature::v8_6a))
        set_bit(features, Feature::v8_5a, true);
    if (test_nbit(features, Feature::v8_5a))
        set_bit(features, Feature::v8_4a, true);
    if (test_nbit(features, Feature::v8_4a))
        set_bit(features, Feature::v8_3a, true);
    if (test_nbit(features, Feature::v8_3a))
        set_bit(features, Feature::v8_2a, true);
    if (test_nbit(features, Feature::v8_2a))
        set_bit(features, Feature::v8_1a, true);
    if (test_nbit(features, Feature::v8_1a))
        set_bit(features, Feature::crc, true);
#ifdef _CPU_ARM_
    if (test_nbit(features, Feature::v8_1a)) {
        set_bit(features, Feature::v8, true);
        set_bit(features, Feature::aclass, true);
    }
    if (test_nbit(features, Feature::v8_m_main)) {
        set_bit(features, Feature::v8, true);
        set_bit(features, Feature::mclass, true);
    }
    if (test_nbit(features, Feature::v8)) {
        set_bit(features, Feature::v7, true);
        if (test_nbit(features, Feature::aclass)) {
            set_bit(features, Feature::neon, true);
            set_bit(features, Feature::vfp3, true);
            set_bit(features, Feature::vfp4, true);
            set_bit(features, Feature::hwdiv_arm, true);
            set_bit(features, Feature::hwdiv, true);
            set_bit(features, Feature::d32, true);
        }
    }
#else
    if (test_nbit(features, Feature::v8_1a)) {
        set_bit(features, Feature::lse, true);
        set_bit(features, Feature::rdm, true);
    }
    if (test_nbit(features, Feature::v8_2a)) {
        set_bit(features, Feature::ccpp, true);
    }
    if (test_nbit(features, Feature::v8_3a)) {
        set_bit(features, Feature::jsconv, true);
        set_bit(features, Feature::complxnum, true);
        set_bit(features, Feature::rcpc, true);
    }
    if (test_nbit(features, Feature::v8_4a)) {
        set_bit(features, Feature::dit, true);
        set_bit(features, Feature::rcpc_immo, true);
        set_bit(features, Feature::flagm, true);
    }
    if (test_nbit(features, Feature::v8_5a)) {
        set_bit(features, Feature::sb, true);
        set_bit(features, Feature::ccdp, true);
        set_bit(features, Feature::altnzcv, true);
        set_bit(features, Feature::fptoint, true);
    }
    if (test_nbit(features, Feature::v8_6a)) {
        set_bit(features, Feature::i8mm, true);
        set_bit(features, Feature::bf16, true);
    }
#endif
    ::enable_depends(features, Feature::deps, sizeof(Feature::deps) / sizeof(FeatureDep));
}

template<size_t n>
static inline void disable_depends(FeatureList<n> &features)
{
    ::disable_depends(features, Feature::deps, sizeof(Feature::deps) / sizeof(FeatureDep));
}

static const llvm::SmallVector<TargetData<feature_sz>, 0> &get_cmdline_targets(const char *cpu_target)
{
    auto feature_cb = [] (const char *str, size_t len, FeatureList<feature_sz> &list) {
#ifdef _CPU_AARCH64_
        // On AArch64, treat `crypto` as an alias of aes + sha2 just like LLVM
        if (llvm::StringRef(str, len) == "crypto") {
            set_bit(list, Feature::aes, true);
            set_bit(list, Feature::sha2, true);
            return true;
        }
#endif
        auto fbit = find_feature_bit(feature_names, nfeature_names, str, len);
        if (fbit == UINT32_MAX)
            return false;
        set_bit(list, fbit, true);
        return true;
    };
    auto &targets = ::get_cmdline_targets<feature_sz>(cpu_target, feature_cb);
    for (auto &t: targets) {
        if (auto nname = normalize_cpu_name(t.name)) {
            t.name = nname;
        }
    }
    return targets;
}

static llvm::SmallVector<TargetData<feature_sz>, 0> jit_targets;

static TargetData<feature_sz> arg_target_data(const TargetData<feature_sz> &arg, bool require_host)
{
    TargetData<feature_sz> res = arg;
    const FeatureList<feature_sz> *cpu_features = nullptr;
    if (res.name == "native") {
        res.name = host_cpu_name();
        cpu_features = &get_host_cpu().second;
    }
    else if (auto spec = find_cpu(res.name)) {
        cpu_features = &spec->features;
    }
    else {
        res.en.flags |= JL_TARGET_UNKNOWN_NAME;
    }
    if (cpu_features) {
        for (size_t i = 0; i < feature_sz; i++) {
            res.en.features[i] |= (*cpu_features)[i];
        }
    }
    enable_depends(res.en.features);
    for (size_t i = 0; i < feature_sz; i++)
        res.en.features[i] &= ~res.dis.features[i];
    if (require_host) {
        for (size_t i = 0; i < feature_sz; i++) {
            res.en.features[i] &= get_host_cpu().second[i];
        }
    }
    disable_depends(res.en.features);
    if (cpu_features) {
        // If the base feature if known, fill in the disable features
        for (size_t i = 0; i < feature_sz; i++) {
            res.dis.features[i] = feature_masks[i] & ~res.en.features[i];
        }
    }
    return res;
}

static int max_vector_size(const FeatureList<feature_sz> &features)
{
#ifdef _CPU_ARM_
    if (test_nbit(features, Feature::neon))
        return 16;
    return 8;
#else
    if (test_nbit(features, Feature::sve2))
        return 256;
    if (test_nbit(features, Feature::sve))
        return 128;
    return 16;
#endif
}

static uint32_t sysimg_init_cb(void *ctx, const void *id, jl_value_t **rejection_reason)
{
    // First see what target is requested for the JIT.
    const char *cpu_target = (const char *)ctx;
    auto &cmdline = get_cmdline_targets(cpu_target);
    TargetData<feature_sz> target = arg_target_data(cmdline[0], true);
    // Then find the best match in the sysimg
    auto sysimg = deserialize_target_data<feature_sz>((const uint8_t*)id);
    for (auto &t: sysimg) {
        if (auto nname = normalize_cpu_name(t.name)) {
            t.name = nname;
        }
    }
    auto match = match_sysimg_targets(sysimg, target, max_vector_size, rejection_reason);
    if (match.best_idx == UINT32_MAX)
        return match.best_idx;
    // Now we've decided on which sysimg version to use.
    // Make sure the JIT target is compatible with it and save the JIT target.
    if (match.vreg_size != max_vector_size(target.en.features) &&
        (sysimg[match.best_idx].en.flags & JL_TARGET_VEC_CALL)) {
#ifdef _CPU_ARM_
        unset_bits(target.en.features, Feature::neon);
#endif
    }
    jit_targets.push_back(std::move(target));
    return match.best_idx;
}

static uint32_t pkgimg_init_cb(void *ctx, const void *id, jl_value_t **rejection_reason JL_REQUIRE_ROOTED_SLOT)
{
    TargetData<feature_sz> target = jit_targets.front();
    auto pkgimg = deserialize_target_data<feature_sz>((const uint8_t*)id);
    for (auto &t: pkgimg) {
        if (auto nname = normalize_cpu_name(t.name)) {
            t.name = nname;
        }
    }
    auto match = match_sysimg_targets(pkgimg, target, max_vector_size, rejection_reason);
    return match.best_idx;
}

static void ensure_jit_target(const char *cpu_target, bool imaging)
{
    auto &cmdline = get_cmdline_targets(cpu_target);
    check_cmdline(cmdline, imaging);
    if (!jit_targets.empty())
        return;
    for (auto &arg: cmdline) {
        auto data = arg_target_data(arg, jit_targets.empty());
        jit_targets.push_back(std::move(data));
    }
    auto ntargets = jit_targets.size();
    // Now decide the clone condition.
    for (size_t i = 1; i < ntargets; i++) {
        auto &t = jit_targets[i];
        if (t.en.flags & JL_TARGET_CLONE_ALL)
            continue;
        auto &features0 = jit_targets[t.base].en.features;
        // Always clone when code checks CPU features
        t.en.flags |= JL_TARGET_CLONE_CPU;
        static constexpr uint32_t clone_fp16[] = {Feature::fp16fml,Feature::fullfp16};
        for (auto fe: clone_fp16) {
            if (!test_nbit(features0, fe) && test_nbit(t.en.features, fe)) {
                t.en.flags |= JL_TARGET_CLONE_FLOAT16;
                break;
            }
        }
        // The most useful one in general...
        t.en.flags |= JL_TARGET_CLONE_LOOP;
#ifdef _CPU_ARM_
        static constexpr uint32_t clone_math[] = {Feature::vfp3, Feature::vfp4, Feature::neon};
        for (auto fe: clone_math) {
            if (!test_nbit(features0, fe) && test_nbit(t.en.features, fe)) {
                t.en.flags |= JL_TARGET_CLONE_MATH;
                break;
            }
        }
        static constexpr uint32_t clone_simd[] = {Feature::neon};
        for (auto fe: clone_simd) {
            if (!test_nbit(features0, fe) && test_nbit(t.en.features, fe)) {
                t.en.flags |= JL_TARGET_CLONE_SIMD;
                break;
            }
        }
#endif
    }
}

static std::pair<std::string,llvm::SmallVector<std::string, 0>>
get_llvm_target_noext(const TargetData<feature_sz> &data)
{
    std::string name = data.name;
    auto *spec = find_cpu(name);
    while (spec) {
        if (spec->llvmver <= JL_LLVM_VERSION)
            break;
        spec = find_cpu((uint32_t)spec->fallback);
        name = spec->name;
    }
    auto features = data.en.features;
    if (spec) {
        if (is_generic_cpu_name((uint32_t)spec->cpu)) {
            features = features | spec->features;
            name = "generic";
        }
    }
#ifdef _CPU_ARM_
    // We use the name on aarch64 internally but the LLVM ARM backend still use the old name...
    if (name == "apple-a7")
        name = "cyclone";
#endif
    llvm::SmallVector<std::string, 0> feature_strs;
    for (auto &fename: feature_names) {
        if (fename.llvmver > JL_LLVM_VERSION)
            continue;
        if (fename.bit >= 32 * 2)
            break;
        const char *fename_str = fename.name;
        bool enable = test_nbit(features, fename.bit);
        bool disable = test_nbit(data.dis.features, fename.bit);
        if (enable) {
            feature_strs.insert(feature_strs.begin(), std::string("+") + fename_str);
        }
        else if (disable) {
            feature_strs.push_back(std::string("-") + fename_str);
        }
    }
    if (test_nbit(features, Feature::v8_6a))
        feature_strs.push_back("+v8.6a");
    if (test_nbit(features, Feature::v8_5a))
        feature_strs.push_back("+v8.5a");
    if (test_nbit(features, Feature::v8_4a))
        feature_strs.push_back("+v8.4a");
    if (test_nbit(features, Feature::v8_3a))
        feature_strs.push_back("+v8.3a");
    if (test_nbit(features, Feature::v8_2a))
        feature_strs.push_back("+v8.2a");
    if (test_nbit(features, Feature::v8_1a))
        feature_strs.push_back("+v8.1a");
#ifdef _CPU_ARM_
    if (test_nbit(features, Feature::v8_m_main)) {
        feature_strs.push_back("+v8m.main");
        feature_strs.push_back("+armv8-m.main");
    }
    if (test_nbit(features, Feature::aclass))
        feature_strs.push_back("+aclass");
    if (test_nbit(features, Feature::rclass))
        feature_strs.push_back("+rclass");
    if (test_nbit(features, Feature::mclass))
        feature_strs.push_back("+mclass");
    if (test_nbit(features, Feature::v8)) {
        feature_strs.push_back("+v8");
        if (test_nbit(features, Feature::aclass))
            feature_strs.push_back("+armv8-a");
        if (test_nbit(features, Feature::rclass))
            feature_strs.push_back("+armv8-r");
        if (test_nbit(features, Feature::mclass)) {
            feature_strs.push_back("+v8m");
            feature_strs.push_back("+armv8-m.base");
        }
    }
    if (test_nbit(features, Feature::v7)) {
        feature_strs.push_back("+v7");
        if (test_nbit(features, Feature::aclass))
            feature_strs.push_back("+armv7-a");
        if (test_nbit(features, Feature::rclass))
            feature_strs.push_back("+armv7-r");
        if (test_nbit(features, Feature::mclass))
            feature_strs.push_back("+armv7-m");
    }
    feature_strs.push_back("+v6");
    feature_strs.push_back("+vfp2");
#else
    feature_strs.push_back("+neon");
    feature_strs.push_back("+fp-armv8");
#endif
    return std::make_pair(std::move(name), std::move(feature_strs));
}

static std::pair<std::string,llvm::SmallVector<std::string, 0>>
get_llvm_target_vec(const TargetData<feature_sz> &data)
{
    auto res0 = get_llvm_target_noext(data);
    append_ext_features(res0.second, data.ext_features);
    return res0;
}

static std::pair<std::string,std::string>
get_llvm_target_str(const TargetData<feature_sz> &data)
{
    auto res0 = get_llvm_target_noext(data);
    auto features = join_feature_strs(res0.second);
    append_ext_features(features, data.ext_features);
    return std::make_pair(std::move(res0.first), std::move(features));
}

static FeatureList<feature_sz> get_max_feature(void)
{
#ifdef _CPU_ARM_
    auto arch = get_elf_arch();
    auto features = real_feature_masks;
    if (arch.klass == 0)
        arch.klass = 'A';
    set_bit(features, Feature::v7, true);
    set_bit(features, Feature::v8, true);
    if (arch.klass == 'M') {
        set_bit(features, Feature::mclass, true);
        set_bit(features, Feature::v8_m_main, true);
    }
    else if (arch.klass == 'R') {
        set_bit(features, Feature::rclass, true);
    }
    else if (arch.klass == 'A') {
        set_bit(features, Feature::aclass, true);
        set_bit(features, Feature::v8_1a, true);
        set_bit(features, Feature::v8_2a, true);
        set_bit(features, Feature::v8_3a, true);
        set_bit(features, Feature::v8_4a, true);
        set_bit(features, Feature::v8_5a, true);
        set_bit(features, Feature::v8_6a, true);
    }
    return features;
#else
    // There isn't currently any conflicting features on AArch64
    return feature_masks;
#endif
}

}

using namespace ARM;

JL_DLLEXPORT void jl_dump_host_cpu(void)
{
    dump_cpu_spec(get_host_cpu().first, get_host_cpu().second, feature_names, nfeature_names,
                  cpus, ncpu_names);
}

JL_DLLEXPORT jl_value_t *jl_cpu_has_fma(int bits)
{
#ifdef _CPU_AARCH64_
    return jl_true;
#else
    TargetData<feature_sz> target = jit_targets.front();
    FeatureList<feature_sz> features = target.en.features;
    if (bits == 32 && test_nbit(features, Feature::vfp4sp))
        return jl_true;
    else if ((bits == 64 || bits == 32) && test_nbit(features, Feature::vfp4))
        return jl_true;
    else
        return jl_false;
#endif
}

jl_image_t jl_init_processor_sysimg(jl_image_buf_t image, const char *cpu_target)
{
    if (!jit_targets.empty())
        jl_error("JIT targets already initialized");
    return parse_sysimg(image, sysimg_init_cb, (void *)cpu_target);
}

jl_image_t jl_init_processor_pkgimg(jl_image_buf_t image)
{
    if (jit_targets.empty())
        jl_error("JIT targets not initialized");
    if (jit_targets.size() > 1)
        jl_error("Expected only one JIT target");
    return parse_sysimg(image, pkgimg_init_cb, NULL);
}

JL_DLLEXPORT jl_value_t* jl_check_pkgimage_clones(char *data)
{
    jl_value_t *rejection_reason = NULL;
    JL_GC_PUSH1(&rejection_reason);
    uint32_t match_idx = pkgimg_init_cb(NULL, data, &rejection_reason);
    JL_GC_POP();
    if (match_idx == UINT32_MAX)
        return rejection_reason;
    return jl_nothing;
}

std::pair<std::string,llvm::SmallVector<std::string, 0>> jl_get_llvm_target(const char *cpu_target, bool imaging, uint32_t &flags)
{
    ensure_jit_target(cpu_target, imaging);
    flags = jit_targets[0].en.flags;
    return get_llvm_target_vec(jit_targets[0]);
}

const std::pair<std::string,std::string> &jl_get_llvm_disasm_target(void)
{
    auto max_feature = get_max_feature();
    static const auto res = get_llvm_target_str(TargetData<feature_sz>{host_cpu_name(),
#ifdef _CPU_AARCH64_
                "+ecv,+tme,+am,+specrestrict,+predres,+lor,+perfmon,+spe,+tracev8.4",
#else
                "+dotprod",
#endif
                {max_feature, 0}, {feature_masks & ~max_feature, 0}, 0});
    return res;
}

#ifndef __clang_gcanalyzer__
llvm::SmallVector<jl_target_spec_t, 0> jl_get_llvm_clone_targets(const char *cpu_target)
{

    auto &cmdline = get_cmdline_targets(cpu_target);
    check_cmdline(cmdline, true);
    llvm::SmallVector<TargetData<feature_sz>, 0> image_targets;
    for (auto &arg: cmdline) {
        auto data = arg_target_data(arg, image_targets.empty());
        image_targets.push_back(std::move(data));
    }
    auto ntargets = image_targets.size();
    if (image_targets.empty())
        jl_error("No targets specified");
    llvm::SmallVector<jl_target_spec_t, 0> res;
    // Now decide the clone condition.
    for (size_t i = 1; i < ntargets; i++) {
        auto &t = image_targets[i];
        if (t.en.flags & JL_TARGET_CLONE_ALL)
            continue;
        auto &features0 = image_targets[t.base].en.features;
        // Always clone when code checks CPU features
        t.en.flags |= JL_TARGET_CLONE_CPU;
        static constexpr uint32_t clone_fp16[] = {Feature::fp16fml,Feature::fullfp16};
        for (auto fe: clone_fp16) {
            if (!test_nbit(features0, fe) && test_nbit(t.en.features, fe)) {
                t.en.flags |= JL_TARGET_CLONE_FLOAT16;
                break;
            }
        }
        // The most useful one in general...
        t.en.flags |= JL_TARGET_CLONE_LOOP;
#ifdef _CPU_ARM_
        static constexpr uint32_t clone_math[] = {Feature::vfp3, Feature::vfp4, Feature::neon};
        for (auto fe: clone_math) {
            if (!test_nbit(features0, fe) && test_nbit(t.en.features, fe)) {
                t.en.flags |= JL_TARGET_CLONE_MATH;
                break;
            }
        }
        static constexpr uint32_t clone_simd[] = {Feature::neon};
        for (auto fe: clone_simd) {
            if (!test_nbit(features0, fe) && test_nbit(t.en.features, fe)) {
                t.en.flags |= JL_TARGET_CLONE_SIMD;
                break;
            }
        }
#endif
    }
    for (auto &target: image_targets) {
        auto features_en = target.en.features;
        auto features_dis = target.dis.features;
        for (auto &fename: feature_names) {
            if (fename.llvmver > JL_LLVM_VERSION) {
                unset_bits(features_en, fename.bit);
                unset_bits(features_dis, fename.bit);
            }
        }
        ARM::disable_depends(features_en);
        jl_target_spec_t ele;
        std::tie(ele.cpu_name, ele.cpu_features) = get_llvm_target_str(target);
        ele.data = serialize_target_data(target.name, features_en, features_dis,
                                         target.ext_features);
        ele.flags = target.en.flags;
        ele.base = target.base;
        res.push_back(ele);
    }
    return res;
}

#endif

extern "C" int jl_test_cpu_feature(jl_cpu_feature_t feature)
{
    if (feature >= 32 * feature_sz)
        return 0;
    return test_nbit(&get_host_cpu().second[0], feature);
}

#ifdef _CPU_AARCH64_
// FPCR FZ, bit [24]
static constexpr uint64_t fpcr_fz_mask = 1 << 24;
// FPCR FZ16, bit [19]
static constexpr uint64_t fpcr_fz16_mask = 1 << 19;
// FPCR DN, bit [25]
static constexpr uint64_t fpcr_dn_mask = 1 << 25;

static inline uint64_t get_fpcr_aarch64(void)
{
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    return fpcr;
}

static inline void set_fpcr_aarch64(uint64_t fpcr)
{
    asm volatile("msr fpcr, %0" :: "r"(fpcr));
}

extern "C" JL_DLLEXPORT int32_t jl_get_zero_subnormals(void)
{
    return (get_fpcr_aarch64() & fpcr_fz_mask) != 0;
}

extern "C" JL_DLLEXPORT int32_t jl_set_zero_subnormals(int8_t isZero)
{
    uint64_t fpcr = get_fpcr_aarch64();
    static uint64_t mask = fpcr_fz_mask | (jl_test_cpu_feature(JL_AArch64_fullfp16) ? fpcr_fz16_mask : 0);
    fpcr = isZero ? (fpcr | mask) : (fpcr & ~mask);
    set_fpcr_aarch64(fpcr);
    return 0;
}

extern "C" JL_DLLEXPORT int32_t jl_get_default_nans(void)
{
    return (get_fpcr_aarch64() & fpcr_dn_mask) != 0;
}

extern "C" JL_DLLEXPORT int32_t jl_set_default_nans(int8_t isDefault)
{
    uint64_t fpcr = get_fpcr_aarch64();
    fpcr = isDefault ? (fpcr | fpcr_dn_mask) : (fpcr & ~fpcr_dn_mask);
    set_fpcr_aarch64(fpcr);
    return 0;
}
#else
extern "C" JL_DLLEXPORT int32_t jl_get_zero_subnormals(void)
{
    return 0;
}

extern "C" JL_DLLEXPORT int32_t jl_set_zero_subnormals(int8_t isZero)
{
    return isZero;
}

extern "C" JL_DLLEXPORT int32_t jl_get_default_nans(void)
{
    return 0;
}

extern "C" JL_DLLEXPORT int32_t jl_set_default_nans(int8_t isDefault)
{
    return isDefault;
}
#endif
