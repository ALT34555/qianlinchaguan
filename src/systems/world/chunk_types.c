#include "chunk_types.h"
#include "precip.h"
#include "../../core/math_util.h"
#include "../../core/i18n.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* 完整区块类型注册表，对齐 ChunkTypes */
static const ChunkTypeDef CHUNK_DEFS[] = {
    {    0, "000", "void", "虚空", 0x2E2E2EFFu, false, false, false, false, false, "虚空占位"},
    {    2, "002", "water", "静水", 0x4B6C8CFFu, true , false, false, false, false, "静水"},
    {    4, "004", "waterfall", "瀑布", 0x8ABCD1FFu, false, true , true , false, false, "瀑落"},
    {    5, "005", "river", "河", 0x7BA4B8FFu, false, true , false, false, false, "河道"},
    {   15, "015", "river_material_1", "岩石河", 0x7BA4B8FFu, false, true , false, false, false, "河道"},
    {   25, "025", "river_material_2", "泥土河", 0x7BA4B8FFu, false, true , false, false, false, "河道"},
    {   35, "035", "river_material_3", "冰雪河", 0x7BA4B8FFu, false, true , false, false, false, "河道"},
    {   45, "045", "river_material_4", "沙地河", 0x7BA4B8FFu, false, true , false, false, false, "河道"},
    {   55, "055", "river_material_5", "碎石河", 0x7BA4B8FFu, false, true , false, false, false, "河道"},
    {   22, "022", "wetland", "湿地", 0x6A8D8CFFu, false, false, false, true , false, "湿地"},
    {   32, "032", "salt_water", "咸静水", 0x627E93FFu, true , false, false, false, false, "静水"},
    {  101, "101", "land_1_0_1", "常年冰封承托物平坦", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  102, "102", "land_1_0_2", "常年冰封承托物凹地", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  103, "103", "land_1_0_3", "常年冰封承托物隆起", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  104, "104", "land_1_0_4", "常年冰封承托物陡峭", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  105, "105", "land_1_0_5", "常年冰封承托物雕刻", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  111, "111", "land_1_1_1", "常年冰封岩平坦", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  112, "112", "land_1_1_2", "常年冰封岩凹地", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  113, "113", "land_1_1_3", "常年冰封岩隆起", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  114, "114", "land_1_1_4", "常年冰封岩陡峭", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  115, "115", "land_1_1_5", "常年冰封岩雕刻", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  121, "121", "land_1_2_1", "常年冰封土壤平坦", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  122, "122", "land_1_2_2", "常年冰封土壤凹地", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  123, "123", "land_1_2_3", "常年冰封土壤隆起", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  124, "124", "land_1_2_4", "常年冰封土壤陡峭", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  125, "125", "land_1_2_5", "常年冰封土壤雕刻", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  131, "131", "land_1_3_1", "常年冰封盐碱土壤平坦", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  132, "132", "land_1_3_2", "常年冰封盐碱土壤凹地", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  133, "133", "land_1_3_3", "常年冰封盐碱土壤隆起", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  134, "134", "land_1_3_4", "常年冰封盐碱土壤陡峭", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  135, "135", "land_1_3_5", "常年冰封盐碱土壤雕刻", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  141, "141", "land_1_4_1", "常年冰封沙平坦", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  142, "142", "land_1_4_2", "常年冰封沙凹地", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  143, "143", "land_1_4_3", "常年冰封沙隆起", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  144, "144", "land_1_4_4", "常年冰封沙陡峭", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  145, "145", "land_1_4_5", "常年冰封沙雕刻", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  151, "151", "land_1_5_1", "常年冰封砾平坦", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  152, "152", "land_1_5_2", "常年冰封砾凹地", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  153, "153", "land_1_5_3", "常年冰封砾隆起", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  154, "154", "land_1_5_4", "常年冰封砾陡峭", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  155, "155", "land_1_5_5", "常年冰封砾雕刻", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  201, "201", "land_2_0_1", "寒带承托物平坦", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  202, "202", "land_2_0_2", "寒带承托物凹地", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  203, "203", "land_2_0_3", "寒带承托物隆起", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  204, "204", "land_2_0_4", "寒带承托物陡峭", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  205, "205", "land_2_0_5", "寒带承托物雕刻", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  211, "211", "land_2_1_1", "寒带岩平坦", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  212, "212", "land_2_1_2", "寒带岩凹地", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  213, "213", "land_2_1_3", "寒带岩隆起", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  214, "214", "land_2_1_4", "寒带岩陡峭", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  215, "215", "land_2_1_5", "寒带岩雕刻", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  221, "221", "land_2_2_1", "寒带土壤平坦", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  222, "222", "land_2_2_2", "寒带土壤凹地", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  223, "223", "land_2_2_3", "寒带土壤隆起", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  224, "224", "land_2_2_4", "寒带土壤陡峭", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  225, "225", "land_2_2_5", "寒带土壤雕刻", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  231, "231", "land_2_3_1", "寒带盐碱土壤平坦", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  232, "232", "land_2_3_2", "寒带盐碱土壤凹地", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  233, "233", "land_2_3_3", "寒带盐碱土壤隆起", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  234, "234", "land_2_3_4", "寒带盐碱土壤陡峭", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  235, "235", "land_2_3_5", "寒带盐碱土壤雕刻", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  241, "241", "land_2_4_1", "寒带沙平坦", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  242, "242", "land_2_4_2", "寒带沙凹地", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  243, "243", "land_2_4_3", "寒带沙隆起", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  244, "244", "land_2_4_4", "寒带沙陡峭", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  245, "245", "land_2_4_5", "寒带沙雕刻", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  251, "251", "land_2_5_1", "寒带砾平坦", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  252, "252", "land_2_5_2", "寒带砾凹地", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  253, "253", "land_2_5_3", "寒带砾隆起", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  254, "254", "land_2_5_4", "寒带砾陡峭", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  255, "255", "land_2_5_5", "寒带砾雕刻", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  301, "301", "land_3_0_1", "温带承托物平坦", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  302, "302", "land_3_0_2", "温带承托物凹地", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  303, "303", "land_3_0_3", "温带承托物隆起", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  304, "304", "land_3_0_4", "温带承托物陡峭", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  305, "305", "land_3_0_5", "温带承托物雕刻", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  311, "311", "land_3_1_1", "温带岩平坦", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  312, "312", "land_3_1_2", "温带岩凹地", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  313, "313", "land_3_1_3", "温带岩隆起", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  314, "314", "land_3_1_4", "温带岩陡峭", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  315, "315", "land_3_1_5", "温带岩雕刻", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  321, "321", "land_3_2_1", "温带土壤平坦", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  322, "322", "land_3_2_2", "温带土壤凹地", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  323, "323", "land_3_2_3", "温带土壤隆起", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  324, "324", "land_3_2_4", "温带土壤陡峭", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  325, "325", "land_3_2_5", "温带土壤雕刻", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  331, "331", "land_3_3_1", "温带盐碱土壤平坦", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  332, "332", "land_3_3_2", "温带盐碱土壤凹地", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  333, "333", "land_3_3_3", "温带盐碱土壤隆起", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  334, "334", "land_3_3_4", "温带盐碱土壤陡峭", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  335, "335", "land_3_3_5", "温带盐碱土壤雕刻", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  341, "341", "land_3_4_1", "温带沙平坦", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  342, "342", "land_3_4_2", "温带沙凹地", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  343, "343", "land_3_4_3", "温带沙隆起", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  344, "344", "land_3_4_4", "温带沙陡峭", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  345, "345", "land_3_4_5", "温带沙雕刻", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  351, "351", "land_3_5_1", "温带砾平坦", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  352, "352", "land_3_5_2", "温带砾凹地", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  353, "353", "land_3_5_3", "温带砾隆起", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  354, "354", "land_3_5_4", "温带砾陡峭", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  355, "355", "land_3_5_5", "温带砾雕刻", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  401, "401", "land_4_0_1", "亚热带承托物平坦", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  402, "402", "land_4_0_2", "亚热带承托物凹地", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  403, "403", "land_4_0_3", "亚热带承托物隆起", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  404, "404", "land_4_0_4", "亚热带承托物陡峭", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  405, "405", "land_4_0_5", "亚热带承托物雕刻", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  411, "411", "land_4_1_1", "亚热带岩平坦", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  412, "412", "land_4_1_2", "亚热带岩凹地", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  413, "413", "land_4_1_3", "亚热带岩隆起", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  414, "414", "land_4_1_4", "亚热带岩陡峭", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  415, "415", "land_4_1_5", "亚热带岩雕刻", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  421, "421", "land_4_2_1", "亚热带土壤平坦", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  422, "422", "land_4_2_2", "亚热带土壤凹地", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  423, "423", "land_4_2_3", "亚热带土壤隆起", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  424, "424", "land_4_2_4", "亚热带土壤陡峭", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  425, "425", "land_4_2_5", "亚热带土壤雕刻", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  431, "431", "land_4_3_1", "亚热带盐碱土壤平坦", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  432, "432", "land_4_3_2", "亚热带盐碱土壤凹地", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  433, "433", "land_4_3_3", "亚热带盐碱土壤隆起", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  434, "434", "land_4_3_4", "亚热带盐碱土壤陡峭", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  435, "435", "land_4_3_5", "亚热带盐碱土壤雕刻", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  441, "441", "land_4_4_1", "亚热带沙平坦", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  442, "442", "land_4_4_2", "亚热带沙凹地", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  443, "443", "land_4_4_3", "亚热带沙隆起", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  444, "444", "land_4_4_4", "亚热带沙陡峭", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  445, "445", "land_4_4_5", "亚热带沙雕刻", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  451, "451", "land_4_5_1", "亚热带砾平坦", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  452, "452", "land_4_5_2", "亚热带砾凹地", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  453, "453", "land_4_5_3", "亚热带砾隆起", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  454, "454", "land_4_5_4", "亚热带砾陡峭", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  455, "455", "land_4_5_5", "亚热带砾雕刻", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  501, "501", "land_5_0_1", "热带承托物平坦", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  502, "502", "land_5_0_2", "热带承托物凹地", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  503, "503", "land_5_0_3", "热带承托物隆起", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  504, "504", "land_5_0_4", "热带承托物陡峭", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  505, "505", "land_5_0_5", "热带承托物雕刻", 0x9B9588FFu, false, false, false, false, false, "陆地"},
    {  511, "511", "land_5_1_1", "热带岩平坦", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  512, "512", "land_5_1_2", "热带岩凹地", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  513, "513", "land_5_1_3", "热带岩隆起", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  514, "514", "land_5_1_4", "热带岩陡峭", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  515, "515", "land_5_1_5", "热带岩雕刻", 0x8A8981FFu, false, false, false, false, false, "陆地"},
    {  521, "521", "land_5_2_1", "热带土壤平坦", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  522, "522", "land_5_2_2", "热带土壤凹地", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  523, "523", "land_5_2_3", "热带土壤隆起", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  524, "524", "land_5_2_4", "热带土壤陡峭", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  525, "525", "land_5_2_5", "热带土壤雕刻", 0xA4B38BFFu, false, false, false, false, false, "陆地"},
    {  531, "531", "land_5_3_1", "热带盐碱土壤平坦", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  532, "532", "land_5_3_2", "热带盐碱土壤凹地", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  533, "533", "land_5_3_3", "热带盐碱土壤隆起", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  534, "534", "land_5_3_4", "热带盐碱土壤陡峭", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  535, "535", "land_5_3_5", "热带盐碱土壤雕刻", 0xC0B896FFu, false, false, false, false, false, "陆地"},
    {  541, "541", "land_5_4_1", "热带沙平坦", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  542, "542", "land_5_4_2", "热带沙凹地", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  543, "543", "land_5_4_3", "热带沙隆起", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  544, "544", "land_5_4_4", "热带沙陡峭", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  545, "545", "land_5_4_5", "热带沙雕刻", 0xD9C7A9FFu, false, false, false, false, false, "陆地"},
    {  551, "551", "land_5_5_1", "热带砾平坦", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  552, "552", "land_5_5_2", "热带砾凹地", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  553, "553", "land_5_5_3", "热带砾隆起", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  554, "554", "land_5_5_4", "热带砾陡峭", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    {  555, "555", "land_5_5_5", "热带砾雕刻", 0xA49D8BFFu, false, false, false, false, false, "陆地"},
    { 9000, "9000", "owned_land", "有主之地", 0x8B6D47FFu, false, false, false, false, true,  "属于某个阵营的土地"},
    { 9001, "9001", "flag_land",  "置旗地",   0x8B614DFFu, false, false, false, false, true,  "新阵营的置旗之地"},
};

static const int CHUNK_DEF_COUNT = (int)(sizeof(CHUNK_DEFS) / sizeof(CHUNK_DEFS[0]));

/* 25 种天气表，逐字段对齐 base */
static const WeatherProfile WEATHER_PROFILES[] = {
    {  0, "000", "普通",         1.0f, 0.00f,  1.0f, 0.00f, 0.00f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {  1, "001", "无云的晴天",   0.0f, 0.00f,  1.0f, 0.00f, 0.00f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {  2, "002", "多云",         1.6f, 0.12f,  1.0f, 0.00f, 0.00f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {  3, "003", "阴天",         2.5f, 0.40f,  1.0f, 0.00f, 0.00f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {  4, "004", "晴朗且微风",   0.35f,0.00f,  1.8f, 0.00f, 0.00f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {100, "100", "小雨",         1.6f, 0.20f,  1.0f, 0.25f, 0.00f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {101, "101", "雨",           2.0f, 0.32f,  1.0f, 0.60f, 0.00f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {102, "102", "大雨",         2.5f, 0.50f,  2.0f, 1.00f, 0.00f, 0.0f, 0.00f, 0.65f, 0x8794A5FFu, false, false},
    {103, "103", "暴风雨",       3.0f, 0.70f, 10.0f, 1.00f, 0.00f, 0.0f, 0.00f, 0.45f, 0x8794A5FFu, false, false},
    {104, "104", "雷雨",         3.0f, 0.60f,  4.0f, 0.80f, 0.00f, 0.0f, 0.00f, 0.60f, 0x8794A5FFu, true,  false},
    {200, "200", "雨夹雪",       2.0f, 0.30f,  1.0f, 0.30f, 0.35f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {201, "201", "小雪",         1.6f, 0.20f,  1.0f, 0.00f, 0.30f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {202, "202", "大雪",         2.5f, 0.40f,  1.0f, 0.00f, 1.00f, 0.0f, 0.00f, 0.60f, 0x8794A5FFu, false, false},
    {203, "203", "暴风雪",       3.0f, 0.55f, 12.0f, 0.00f, 1.00f, 0.0f, 0.00f, 0.25f, 0xBBC4CFFFu, false, false},
    {204, "204", "冰雹",         2.5f, 0.45f,  1.0f, 0.00f, 0.00f, 1.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {300, "300", "风",           1.0f, 0.00f,  4.0f, 0.00f, 0.00f, 0.0f, 0.12f, 1.00f, 0x8794A5FFu, false, false},
    {301, "301", "大风",         1.8f, 0.00f,  9.0f, 0.00f, 0.00f, 0.0f, 0.30f, 1.00f, 0x8794A5FFu, false, false},
    {302, "302", "飓风",         3.0f, 0.65f, 18.0f, 0.00f, 0.00f, 0.0f, 0.60f, 0.35f, 0x8794A5FFu, false, true },
    {303, "303", "台风",         3.0f, 0.65f, 15.0f, 1.00f, 0.00f, 0.0f, 0.00f, 0.40f, 0x8794A5FFu, false, true },
    {304, "304", "沙尘暴",       0.5f, 0.50f, 12.0f, 0.00f, 0.00f, 0.0f, 1.00f, 0.20f, 0xB99B68FFu, false, false},
    {400, "400", "雾",           0.6f, 0.00f,  1.0f, 0.00f, 0.00f, 0.0f, 0.00f, 0.18f, 0xC3CCD3FFu, false, false},
    {401, "401", "霾",           0.6f, 0.25f,  1.0f, 0.00f, 0.00f, 0.0f, 0.00f, 0.22f, 0x888888FFu, false, false},
    {900, "900", "日月同耀",     0.3f, 0.00f,  1.0f, 0.00f, 0.00f, 0.0f, 0.00f, 1.00f, 0x6E4D7EFFu, false, false},
    {901, "901", "极昼",         0.5f, 0.00f,  1.0f, 0.00f, 0.00f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false},
    {902, "902", "极夜",         0.5f, 0.00f,  1.0f, 0.00f, 0.00f, 0.0f, 0.00f, 1.00f, 0x8794A5FFu, false, false}
};

static const int WEATHER_COUNT = (int)(sizeof(WEATHER_PROFILES) / sizeof(WEATHER_PROFILES[0]));

/* 查找区块类型定义 */
const ChunkTypeDef *qlcg_get_chunk_type(int type_id) {
    for (int i = 0; i < CHUNK_DEF_COUNT; ++i) {
        if (CHUNK_DEFS[i].id == type_id) return &CHUNK_DEFS[i];
    }
    return &CHUNK_DEFS[1];
}

/* 区块内容按当前语言显示 */
static const char *chunk_localized_text(const ChunkTypeDef *def,
                                        const char *suffix, const char *fallback) {
    char key[80];
    snprintf(key, sizeof(key), "chunk.%s.%s", def->key, suffix);
    const char *text = tr(key);
    return strcmp(text, key) == 0 ? fallback : text;
}

const char *qlcg_chunk_type_name(const ChunkTypeDef *def) {
    if (!def) def = qlcg_get_chunk_type(0);
    return chunk_localized_text(def, "name", def->name);
}

const char *qlcg_chunk_type_description(const ChunkTypeDef *def) {
    if (!def) def = qlcg_get_chunk_type(0);
    return chunk_localized_text(def, "desc", def->desc);
}

/* 权重平滑 */
static float smooth_step(float a, float b, float v) {
    float t = qlcg_clampf((v - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* 取最大权重得材质：1 岩 2 土 3 盐碱 4 沙 5 砾 */
static int land_material(float temp, float aridity, float relief, float slope, float relative) {
    float exposure = 0.28f + slope * 0.7f
                   + qlcg_clampf((relief - 3.0f) / 20.0f, 0.0f, 1.0f) * 0.18f
                   + qlcg_clampf(relative / 3.0f, 0.0f, 1.0f) * 0.05f;
    float rock   = smooth_step(0.50f, 0.78f, exposure);
    float rest   = 1.0f - rock;
    /* 越干越沙，中等干旱积盐，湿润成土，坡地出砾 */
    float sand   = rest * (1.0f - smooth_step(0.22f, 0.50f, aridity));
    float saline = (rest - sand) * smooth_step(0.45f, 0.72f, aridity)
                   * (1.0f - smooth_step(0.95f, 1.30f, aridity));
    float gravel = (rest - sand - saline) * smooth_step(0.30f, 0.60f, exposure)
                   * (1.0f - smooth_step(0.60f, 1.00f, aridity));
    float soil   = rest - sand - saline - gravel;
    /* 冰封带：沙转岩砾，土与盐碱让位 */
    if (temp < -8.0f) {
        float moved = sand * 0.72f;
        sand   -= moved;
        rock   += moved * 0.55f;
        gravel += moved * 0.45f;
        rock   += rest * 0.18f;
        gravel += rest * 0.12f;
        soil   *= 0.30f;
        saline *= 0.50f;
    }
    /* 顺序 1 岩 / 2 土 / 3 盐碱 / 4 沙 / 5 砾 */
    float w[6] = {0.0f, rock, soil, saline, sand, gravel};
    int best = 1;
    for (int i = 2; i <= 5; i++) if (w[i] > w[best]) best = i;
    return best;
}

/* 分类：base 用 温度带*100 + 材质*10 + 地形 */
int qlcg_classify_chunk(bool is_water, bool is_river, bool is_fall, bool is_wetland,
                        bool is_saline, float elev, float temp, float aridity,
                        float relief, float slope, float relative, int claim_type) {
    (void)elev;
    if (claim_type == 2) return 9001;
    if (claim_type == 1) return 9000;
    int mat = land_material(temp, aridity, relief, slope, relative);
    if (is_fall)    return 4;               /* 瀑布 */
    if (is_river)   return 5 + mat * 10;    /* 河：5/15/25/35/45/55 */
    if (is_wetland) return 22;              /* 湿地 */
    if (is_water)   return is_saline ? 32 : 2;
    int lc = (temp < -8.0f) ? 1 : (temp < 8.0f) ? 2 : (temp < 18.0f) ? 3 : (temp < 24.0f) ? 4 : 5;
    int ter = (slope >= 0.7f) ? 4 : (relief < 4.0f) ? 1 : (relative < -0.5f) ? 2 : 3;
    return lc * 100 + mat * 10 + ter;
}

/* 区块类型→自然基质 1..5；无效 0 */
int qlcg_chunk_material(int type_id) {
    int mat = 0;
    if (type_id >= 100 && type_id <= 555) {
        mat = (type_id / 10) % 10;
    } else if (type_id >= 15 && type_id <= 55 && type_id % 10 == 5) {
        mat = (type_id / 10) % 10;
    } else if (type_id == 2) {
        return 2;
    } else if (type_id == 32) {
        return 3;
    }
    return (mat >= 1 && mat <= 5) ? mat : 0;
}

/* 形态明度：平坦亮、凹地暗、陡峭最暗 */
static float display_light(int terrain) {
    switch (terrain) {
    case 1: return 1.06f;
    case 2: return 0.90f;
    case 3: return 0.98f;
    case 4: return 0.80f;
    case 5: return 0.93f;
    default: return 1.0f;
    }
}

/* 区块显示色：材质底色调形态明暗 */
uint32_t qlcg_chunk_display_color(const ChunkTypeDef *def) {
    if (!def) return 0x9B9588FFu;
    uint32_t color = def->map_color;
    int id = def->id;
    if (id < 100 || id > 555) return color;
    int material = (id / 10) % 10;
    int terrain = id % 10;
    if (material < 1 || terrain < 1 || terrain > 5) return color;
    float r = (float)((color >> 24) & 0xFFu);
    float g = (float)((color >> 16) & 0xFFu);
    float b = (float)((color >> 8) & 0xFFu);
    float k = display_light(terrain);
    r *= k; g *= k; b *= k;
    if (terrain == 4) { r *= 0.97f; b *= 1.05f; }
    int ri = qlcg_clampi((int)lroundf(r), 0, 255);
    int gi = qlcg_clampi((int)lroundf(g), 0, 255);
    int bi = qlcg_clampi((int)lroundf(b), 0, 255);
    return ((uint32_t)ri << 24) | ((uint32_t)gi << 16) | ((uint32_t)bi << 8) | 0xFFu;
}

/* 五大气候带名称与颜色 */
const char *qlcg_climate_band_name(int band) {
    static const char *KEYS[5] = {"climate.0", "climate.1", "climate.2", "climate.3", "climate.4"};
    return tr(KEYS[qlcg_clampi(band, 0, 4)]);
}

uint32_t qlcg_climate_band_color(int band) {
    static const uint32_t COLS[5] = {
        0xD97B48FFu, 0x8FBF6AFFu, 0x6FA882FFu, 0x6496B4FFu, 0xB8CFE0FFu
    };
    return COLS[qlcg_clampi(band, 0, 4)];
}

int qlcg_climate_band_of_temp(float temp) {
    if (temp >= 23.0f) return 0;
    if (temp >= 16.0f) return 1;
    if (temp >=  9.0f) return 2;
    if (temp >=  0.0f) return 3;
    return 4;
}

/* 查找天气配置 */
const WeatherProfile *qlcg_get_weather_profile(int weather_id) {
    for (int i = 0; i < WEATHER_COUNT; ++i) {
        if (WEATHER_PROFILES[i].id == weather_id) return &WEATHER_PROFILES[i];
    }
    return &WEATHER_PROFILES[0];
}

void qlcg_weather_init(WeatherSystem *ws, uint32_t seed) {
    ws->seed = seed;
    ws->forced_id = -1;
    ws->current_id = 0;
    ws->world = NULL;
    ws->precip = NULL;
}

void qlcg_weather_bind_world(WeatherSystem *ws, const MacroWorld *w) {
    if (!ws) return;
    ws->world = w;
    ws->precip = w ? w->precip : NULL;
}

/* 换世界只换种子，保留手动天气 */
void qlcg_weather_set_seed(WeatherSystem *ws, uint32_t seed) {
    if (ws) ws->seed = seed;
}

static int wrap_int(int v, int period) {
    int r = v % period;
    return r < 0 ? r + period : r;
}

/* 周期环绕的云噪声，period 为归一化坐标周期 */
static float cloud_noise(uint32_t seed, float x, float z, int period) {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    float fx = x - (float)ix, fz = z - (float)iz;
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fz * fz * (3.0f - 2.0f * fz);
    int x0 = period > 0 ? wrap_int(ix, period) : ix;
    int x1 = period > 0 ? wrap_int(ix + 1, period) : ix + 1;
    float a = qlcg_mixf(qlcg_hash2(x0, iz, seed), qlcg_hash2(x1, iz, seed), u);
    float b = qlcg_mixf(qlcg_hash2(x0, iz + 1, seed), qlcg_hash2(x1, iz + 1, seed), u);
    return qlcg_mixf(a, b, v);
}

/* 场优先，云噪声兜底 */
ChunkWeather qlcg_sample_chunk_weather(const WeatherSystem *ws, double unix_ms,
                                       int cx, int cz, float temp, int eq_chunks) {
    if (eq_chunks > 0) {
        cx = wrap_int(cx, eq_chunks);
        int half = eq_chunks / 4;
        if (cz < -half) cz = -half;
        if (cz > half - 1) cz = half - 1;
    }
    if (ws->forced_id >= 0) {
        const WeatherProfile *prof = qlcg_get_weather_profile(ws->forced_id);
        float inten = qlcg_clampf(prof->rain + prof->snow + prof->hail, 0.0f, 1.0f);
        ChunkWeather cw = {cx, cz, ws->forced_id, qlcg_clampf(prof->clouds / 2.5f, 0.0f, 1.0f),
                           inten, temp, prof};
        return cw;
    }
    if (ws->precip && ws->precip->ready && ws->world && ws->world->columns > 0) {
        const MacroWorld *w = ws->world;
        float circ = w->circumference > 1.0f ? w->circumference : 1.0f;
        float wx = ((float)cx + 0.5f) * (float)CHUNK_SIZE;
        float wz = ((float)cz + 0.5f) * (float)CHUNK_SIZE;
        int cell = qlcg_world_cell_at_wxz(w, wx, wz);
        float st = qlcg_precip_season_temp(w, cell, w->plant_calendar);
        float lon = wx / circ * 6.2831853f;
        float lat = -wz / qlcg_world_depth(w) * 3.1415926f;
        PrecipSample ps;
        if (qlcg_precip_sample(ws->precip, unix_ms, lon, lat, 0.0f, st, &ps)) {
            const WeatherProfile *prof = qlcg_get_weather_profile(ps.id);
            ChunkWeather cw = {cx, cz, ps.id, ps.cloud_cover, ps.intensity, st, prof};
            return cw;
        }
    }
    double minute = floor(unix_ms / 60000.0) * 60000.0;
    float t_hours = (float)(minute / (4.0 * 3600000.0));
    float drift = t_hours * 8.0f;
    int period = eq_chunks > 0 ? eq_chunks / 64 : 0;
    float cover = cloud_noise(ws->seed, ((float)cx + 0.5f - drift) / 64.0f,
                              ((float)cz + 0.5f - drift * 0.34f) / 64.0f, period);
    float intensity = (cover <= 0.56f) ? 0.0f : fminf(1.0f, (cover - 0.56f) / 0.3f);
    int wid;
    if (intensity <= 0.0f) {
        wid = (cover < 0.18f) ? 1 : (cover < 0.30f) ? 4 : (cover < 0.40f) ? 0
            : (cover < 0.50f) ? 2 : 3;
    } else if (temp > 2.0f) {
        wid = (intensity < 0.25f) ? 100 : (intensity < 0.60f) ? 101 : (intensity < 0.90f) ? 102
            : (qlcg_hash2((int)floorf((float)cx / 64.0f), (int)floorf((float)cz / 64.0f),
                          ws->seed) < 0.5f) ? 103 : 104;
    } else if (temp > 0.0f) {
        wid = 200;
    } else {
        wid = (intensity < 0.55f) ? 201 : 202;
    }
    const WeatherProfile *prof = qlcg_get_weather_profile(wid);
    ChunkWeather cw = {cx, cz, wid, cover, intensity, temp, prof};
    return cw;
}

float qlcg_weather_precipitation(int weather_id) {
    const WeatherProfile *p = qlcg_get_weather_profile(weather_id);
    return qlcg_clampf(p->rain + p->snow + p->hail, 0.0f, 1.0f);
}

/* 900/901 固定正午，902 固定午夜 */
float qlcg_weather_day_ratio(int weather_id, float ratio) {
    if (weather_id == 900 || weather_id == 901) return 0.5f;
    if (weather_id == 902) return 0.0f;
    return ratio;
}

/* 风速等级 0..5，未定义默认 0 */
int qlcg_natural_physics_level(int weather_id) {
    switch (weather_id) {
    case 0: case 1: case 2: case 3:
    case 100: case 200: case 201: case 401:
        return 1;
    case 4: case 101: case 102: case 202:
        return 2;
    case 104: case 204: case 300:
        return 3;
    case 103: case 203: case 301:
        return 4;
    case 302: case 303: case 304:
        return 5;
    default:
        return 0;
    }
}

/* 执行 /world.weather 天气指令 */
bool qlcg_weather_command(WeatherSystem *ws, const char *cmd, char *reply, size_t cap) {
    if (strncmp(cmd, "/world.weather", 14) != 0) return false;
    if (cmd[14] != '\0' && cmd[14] != ' ') return false;
    const char *arg = cmd + 14;
    while (*arg == ' ') arg++;
    if (!*arg || strcmp(arg, "reset") == 0 || strcmp(arg, "clear") == 0) {
        ws->forced_id = -1;
        snprintf(reply, cap, tr("weather.reset"));
        return true;
    }
    int id = atoi(arg);
    for (int i = 0; i < WEATHER_COUNT; ++i) {
        if (WEATHER_PROFILES[i].id == id) {
            ws->forced_id = id;
            ws->current_id = id;
            snprintf(reply, cap, tr("weather.forced"), WEATHER_PROFILES[i].code, WEATHER_PROFILES[i].name);
            return true;
        }
    }
    snprintf(reply, cap, tr("weather.usage"));
    return true;
}
