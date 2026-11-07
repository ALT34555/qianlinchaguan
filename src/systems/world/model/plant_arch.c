/* 物种模型分发 */
#include "plant_arch.h"
#include <string.h>

void qlcg_plant_cao_bamao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_dixue(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_gouyagen(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_hanji(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_huweilan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_jianpeng(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_jiji(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_jue(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_luhui(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_luobuma(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_maozhu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_shamo(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_yangmao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_cao_yanjiao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_aili(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_baici(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_chengliu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_dujuan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_fusang(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_guanggun(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_heiguo(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_huanglu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_jianma(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_lianqiao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_moli(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_shamomeigui(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_suosuo(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_xianrenzhu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_xuangouzi(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_yueju(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_zhen(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_guan_zhizi(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_hua_ju(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_hua_lan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_hua_mei(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_hua_zhu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_nong_da(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_nong_dao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_nong_mai(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_nong_sang(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_nong_su(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_baigan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_bailan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_bajiao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_binglang(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_cebai(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_cha(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_chamu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_chuiliu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_chuizhi(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_fenghuang(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_ganju(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_gaoshan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_haizao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_hongpi(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_hongshu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_hongsong(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_houmianbao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_huai(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_huangbo(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_huyang(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_jinheguan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_lengshan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_lizhi(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_longnao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_luoye(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_mangguo(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_masan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_mumahuang(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_mumian(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_paotong(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_qingtan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_qiu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_rongshu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_shazao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_shi(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_shuiqueliu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_shuishan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_wujiaofeng(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_xiangzhang(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_yangzijing(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_yezi(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_yinxing(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_yunshan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_ziduan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shu_zishu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_biaocao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_changpu(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_chuanman(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_dayezao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_fengyanlan(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_kucao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_lian(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_luwei(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_luweicong(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_luweidang(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_micao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_pingpeng(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_pucao(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_redaishuilian(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_redaitun(LpBuilder *, const PdDef *, int, int);
void qlcg_plant_shui_wanglian(LpBuilder *, const PdDef *, int, int);

static const struct {
    const char *id;
    void (*build)(LpBuilder *, const PdDef *, int, int);
} PLANT_MODELS[] = {
    { "cao.bamao", qlcg_plant_cao_bamao },
    { "cao.dixue", qlcg_plant_cao_dixue },
    { "cao.gouyagen", qlcg_plant_cao_gouyagen },
    { "cao.hanji", qlcg_plant_cao_hanji },
    { "cao.huweilan", qlcg_plant_cao_huweilan },
    { "cao.jianpeng", qlcg_plant_cao_jianpeng },
    { "cao.jiji", qlcg_plant_cao_jiji },
    { "cao.jue", qlcg_plant_cao_jue },
    { "cao.luhui", qlcg_plant_cao_luhui },
    { "cao.luobuma", qlcg_plant_cao_luobuma },
    { "cao.maozhu", qlcg_plant_cao_maozhu },
    { "cao.shamo", qlcg_plant_cao_shamo },
    { "cao.yangmao", qlcg_plant_cao_yangmao },
    { "cao.yanjiao", qlcg_plant_cao_yanjiao },
    { "guan.aili", qlcg_plant_guan_aili },
    { "guan.baici", qlcg_plant_guan_baici },
    { "guan.chengliu", qlcg_plant_guan_chengliu },
    { "guan.dujuan", qlcg_plant_guan_dujuan },
    { "guan.fusang", qlcg_plant_guan_fusang },
    { "guan.guanggun", qlcg_plant_guan_guanggun },
    { "guan.heiguo", qlcg_plant_guan_heiguo },
    { "guan.huanglu", qlcg_plant_guan_huanglu },
    { "guan.jianma", qlcg_plant_guan_jianma },
    { "guan.lianqiao", qlcg_plant_guan_lianqiao },
    { "guan.moli", qlcg_plant_guan_moli },
    { "guan.shamomeigui", qlcg_plant_guan_shamomeigui },
    { "guan.suosuo", qlcg_plant_guan_suosuo },
    { "guan.xianrenzhu", qlcg_plant_guan_xianrenzhu },
    { "guan.xuangouzi", qlcg_plant_guan_xuangouzi },
    { "guan.yueju", qlcg_plant_guan_yueju },
    { "guan.zhen", qlcg_plant_guan_zhen },
    { "guan.zhizi", qlcg_plant_guan_zhizi },
    { "hua.ju", qlcg_plant_hua_ju },
    { "hua.lan", qlcg_plant_hua_lan },
    { "hua.mei", qlcg_plant_hua_mei },
    { "hua.zhu", qlcg_plant_hua_zhu },
    { "nong.da", qlcg_plant_nong_da },
    { "nong.dao", qlcg_plant_nong_dao },
    { "nong.mai", qlcg_plant_nong_mai },
    { "nong.sang", qlcg_plant_nong_sang },
    { "nong.su", qlcg_plant_nong_su },
    { "shu.baigan", qlcg_plant_shu_baigan },
    { "shu.bailan", qlcg_plant_shu_bailan },
    { "shu.bajiao", qlcg_plant_shu_bajiao },
    { "shu.binglang", qlcg_plant_shu_binglang },
    { "shu.cebai", qlcg_plant_shu_cebai },
    { "shu.cha", qlcg_plant_shu_cha },
    { "shu.chamu", qlcg_plant_shu_chamu },
    { "shu.chuiliu", qlcg_plant_shu_chuiliu },
    { "shu.chuizhi", qlcg_plant_shu_chuizhi },
    { "shu.fenghuang", qlcg_plant_shu_fenghuang },
    { "shu.ganju", qlcg_plant_shu_ganju },
    { "shu.gaoshan", qlcg_plant_shu_gaoshan },
    { "shu.haizao", qlcg_plant_shu_haizao },
    { "shu.hongpi", qlcg_plant_shu_hongpi },
    { "shu.hongshu", qlcg_plant_shu_hongshu },
    { "shu.hongsong", qlcg_plant_shu_hongsong },
    { "shu.houmianbao", qlcg_plant_shu_houmianbao },
    { "shu.huai", qlcg_plant_shu_huai },
    { "shu.huangbo", qlcg_plant_shu_huangbo },
    { "shu.huyang", qlcg_plant_shu_huyang },
    { "shu.jinheguan", qlcg_plant_shu_jinheguan },
    { "shu.lengshan", qlcg_plant_shu_lengshan },
    { "shu.lizhi", qlcg_plant_shu_lizhi },
    { "shu.longnao", qlcg_plant_shu_longnao },
    { "shu.luoye", qlcg_plant_shu_luoye },
    { "shu.mangguo", qlcg_plant_shu_mangguo },
    { "shu.masan", qlcg_plant_shu_masan },
    { "shu.mumahuang", qlcg_plant_shu_mumahuang },
    { "shu.mumian", qlcg_plant_shu_mumian },
    { "shu.paotong", qlcg_plant_shu_paotong },
    { "shu.qingtan", qlcg_plant_shu_qingtan },
    { "shu.qiu", qlcg_plant_shu_qiu },
    { "shu.rongshu", qlcg_plant_shu_rongshu },
    { "shu.shazao", qlcg_plant_shu_shazao },
    { "shu.shi", qlcg_plant_shu_shi },
    { "shu.shuiqueliu", qlcg_plant_shu_shuiqueliu },
    { "shu.shuishan", qlcg_plant_shu_shuishan },
    { "shu.wujiaofeng", qlcg_plant_shu_wujiaofeng },
    { "shu.xiangzhang", qlcg_plant_shu_xiangzhang },
    { "shu.yangzijing", qlcg_plant_shu_yangzijing },
    { "shu.yezi", qlcg_plant_shu_yezi },
    { "shu.yinxing", qlcg_plant_shu_yinxing },
    { "shu.yunshan", qlcg_plant_shu_yunshan },
    { "shu.ziduan", qlcg_plant_shu_ziduan },
    { "shu.zishu", qlcg_plant_shu_zishu },
    { "shui.biaocao", qlcg_plant_shui_biaocao },
    { "shui.changpu", qlcg_plant_shui_changpu },
    { "shui.chuanman", qlcg_plant_shui_chuanman },
    { "shui.dayezao", qlcg_plant_shui_dayezao },
    { "shui.fengyanlan", qlcg_plant_shui_fengyanlan },
    { "shui.kucao", qlcg_plant_shui_kucao },
    { "shui.lian", qlcg_plant_shui_lian },
    { "shui.luwei", qlcg_plant_shui_luwei },
    { "shui.luweicong", qlcg_plant_shui_luweicong },
    { "shui.luweidang", qlcg_plant_shui_luweidang },
    { "shui.micao", qlcg_plant_shui_micao },
    { "shui.pingpeng", qlcg_plant_shui_pingpeng },
    { "shui.pucao", qlcg_plant_shui_pucao },
    { "shui.redaishuilian", qlcg_plant_shui_redaishuilian },
    { "shui.redaitun", qlcg_plant_shui_redaitun },
    { "shui.wanglian", qlcg_plant_shui_wanglian },
};

int qlcg_plant_species_build(const PdDef *def, int season, int form, LpBuilder *b) {
    size_t length = strcspn(def->id, "@");
    int lo = 0, hi = sizeof(PLANT_MODELS) / sizeof(PLANT_MODELS[0]);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        const char *id = PLANT_MODELS[mid].id;
        int order = strncmp(def->id, id, length);
        if (order == 0 && id[length] != '\0') order = -1;
        if (order < 0) hi = mid;
        else if (order > 0) lo = mid + 1;
        else {
            PLANT_MODELS[mid].build(b, def, season, form);
            return 1;
        }
    }
    return 0;
}
