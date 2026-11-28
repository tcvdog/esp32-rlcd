# 层级得分与提示扣分

## 加分
每通过一级：
```text
L1 +10
L2 +15
L3 +20
L4 +25
L5 +30
L6 +35
```

## 减分
每使用一次原文提示：
```text
-3 分
```
立即记录，立即扣分，不等到升级。

## 事件
```json
{"type":"score","level":3,"delta":-3,"total":42,"reveals":1}
{"type":"score","level":3,"delta":20,"total":62,"reveals":1}
```

## 净分
总分 = 所有加分 + 所有扣分。
统计页直接显示净分。
