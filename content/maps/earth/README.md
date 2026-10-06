# 内置地球大陆数据

`land.json` 是 Natural Earth 1:110m land 的 720×360 海岸距离场（经度 −180～180，纬度 90～−90，像元中心采样）。`coastDistance` 为 Base64 编码的 Uint8，128 为零线，正值陆地、负值海洋，单位约 0.5°；经度边界循环。使用双线性插值查询。主要山系与气候近似由 `PlanetTerrain.ts` 提供。

来源：[Natural Earth land GeoJSON](https://github.com/nvkelso/natural-earth-vector/blob/master/geojson/ne_110m_land.geojson)。许可：公共领域，见 [Natural Earth 许可](https://github.com/nvkelso/natural-earth-vector/blob/master/LICENSE.md)。下载日期：2026-10-05。仅含自然陆地轮廓，不含行政边界。
