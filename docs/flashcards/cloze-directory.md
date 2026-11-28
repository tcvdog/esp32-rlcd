# 镂空背诵目录、统计与分析文件

## 目录
闪卡 → 05 诗词古文：

```text
01 出师表
02 小学古诗词（8）
03 初中古诗词（6）
04 文言文（6）
05 学习统计
```

所有条目统一复用《出师表》引擎：

```text
20px 专用 XBM 字形
标题/作者同一行
原位白圈遮挡
7 行/页分页
K3 原文/镂空
上下调级，左右翻页
```

## 统计
统计页显示：

```text
ENTER / EXIT
ANSWER K3
L1..L5 进入次数
每个级别访问过哪些页、各多少次
```

数据保存在：

```text
NVS namespace: cloze
/sdcard/cloze_stats.csv
```

CSV 字段：

```text
time,event,group,title,level,page
```

电脑访问：

```text
http://<板子IP>/cloze_stats.csv
```

## 说明
- 进入卡片、退出卡片、切级、翻页、切换答案都会记录。
- 进入/退出按卡片会话计数；切篇会先记 exit 再记 enter。
- 页面访问只记录有变化的页，不按帧重复累加。
- 统计计数器掉电保留在 NVS；逐事件 CSV 在 SD 卡。
