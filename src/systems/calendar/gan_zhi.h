/* 干支与日名，对齐 base 的 GanZhi.ts */
#ifndef QLCG_GAN_ZHI_H
#define QLCG_GAN_ZHI_H

const char *qlcg_stem_name(int index);
const char *qlcg_branch_name(int index);
/* 六十甲子名写入 buf（至少 8 字节） */
void qlcg_gan_zhi_name(int index, char *buf, int cap);

void qlcg_chinese_number(int n, char *buf, int cap);
void qlcg_chinese_day_name(int day, char *buf, int cap);

int  qlcg_year_gan_zhi_index(int year);
const char *qlcg_zodiac_of_year(int year);
int  qlcg_day_gan_zhi_index(int jdn);
void qlcg_day_gan_zhi_name(int jdn, char *buf, int cap);

const char *qlcg_solar_term_name(int index);
/* 按名称取静态表指针，未知名返回 NULL */
const char *qlcg_solar_term_name_at(const char *name);
const char *qlcg_season_name(int index);

#endif
