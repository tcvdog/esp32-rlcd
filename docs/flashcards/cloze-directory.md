# 镂空背诵目录与统计（v0.67.2）

## 分级 × 类型

```text
01 出师表
02 小学古诗      8 首
03 小学文言文    2 篇
04 初中古诗      6 首
05 初中文言文    6 篇
06 高中古诗      6 首
07 高中文言文    6 篇
08 学习统计
```

## 统一引擎
所有条目都使用《出师表》模式：

```text
20px 专用 XBM 字库
标题/作者同一行
原文原位绘制 + 白格黑圈
整词遮挡，不拆词
7 行/页
上下调级、左右翻页
K3 原文/镂空
```

## 统计
- NVS：进入、退出、级别、页面、答案切换计数。
- CSV：`/sdcard/cloze_stats.csv`
- 电脑：`http://<板子IP>/cloze_stats.csv`
- 字段：`time,event,group,title,level,page`

## 资源
- 字库：`chushi_font20.h`（约 40 KB，全部内容共用）
- 文本：`cloze_text.h`
- 统计：`cloze_stats.cpp`
