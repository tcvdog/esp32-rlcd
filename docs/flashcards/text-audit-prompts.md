# 中小学古诗文校验提示词

## 使用原则

1. 一次只校验一篇，不批量让模型“凭印象”判断。
2. 没有教材目录或权威文本时，必须输出 `unknown`，不能猜。
3. 输出固定 JSON，便于脚本汇总。
4. 先自动分诊，再人工对照教材扫描页。
5. “常见古诗文”不等于“中小学教材篇目”。

## Prompt A：单篇教材归属

```text
你是中小学语文教材目录校对员。只根据你确定知道的统编版/人教版教材目录判断，不要因为这首诗很有名就判断为教材篇目。

输入：
标题：{{title}}
作者：{{author}}
学段：{{stage}}
类型：{{poetry_or_prose}}
正文：
{{text}}

请输出 JSON：
{
  "title": "",
  "author": "",
  "is_school_text": true/false/unknown,
  "textbook": "统编版/人教版/其他/unknown",
  "stage": "小学/初中/高中/unknown",
  "grade": "年级/册次/unknown",
  "unit": "单元/unknown",
  "required": "必背/选读/unknown",
  "reason": "不超过80字",
  "confidence": 0.0-1.0,
  "source": "教材目录/权威教辅/unknown"
}

判定要求：
- 作者或正文与教材版本不一致时，is_school_text 仍可为 true，但 reason 必须写明版本差异。
- 不能确认年级/单元就写 unknown，不要编造。
- 非教材常见篇目必须判 false，不要用“经典”替代“教材”。
```

## Prompt B：同名作品碰撞

```text
你是古诗文同名篇目鉴别员。

输入：
标题：{{title}}
当前作者：{{author}}
当前正文：
{{text}}

任务：
1. 列出你确定存在的同标题作品及作者。
2. 判断当前正文属于哪一位作者。
3. 判断是否把同名作品错配成了教材版本。

输出 JSON：
{
  "title": "",
  "current_author": "",
  "same_title_works": [
    {"author": "", "dynasty": "", "first_line": ""}
  ],
  "matched_author": "",
  "is_mismatch": true/false/unknown,
  "textbook_author": "",
  "reason": "",
  "confidence": 0.0-1.0
}
```

## Prompt C：全文完整性

```text
你是教材文本校对员。下面给出“教材参考文本”和“当前文本”，请逐字比较。

教材参考文本：
{{reference}}

当前文本：
{{current}}

输出 JSON：
{
  "is_full_text": true/false/unknown,
  "missing": ["缺失的字词或句子"],
  "extra": ["多出的字词或句子"],
  "wrong_chars": [
    {"current": "", "expected": "", "context": ""}
  ],
  "punctuation_issues": [],
  "paragraph_issues": [],
  "summary": "",
  "confidence": 0.0-1.0
}

要求：
- 只比较，不改写。
- 没有参考文本时全部输出 unknown。
- 不允许用记忆补全参考文本。
```

## Prompt D：学段与年级

```text
你是中小学语文教材学段标注员。
只判断以下篇目在统编版/人教版教材中的学段、年级、册次。

标题：{{title}}
作者：{{author}}
正文：
{{text}}

输出 JSON：
{
  "title": "",
  "stage": "小学低年级/小学中高年级/初中/高中/unknown",
  "grade": "",
  "volume": "",
  "unit": "",
  "evidence": "教材目录中的位置；不确定写 unknown",
  "confidence": 0.0-1.0
}

不要根据文字难度猜学段；没有目录依据就写 unknown。
```

## Prompt E：批量目录分诊

```text
你是教材目录审计员。下面是一批“标题 + 作者 + 学段”。

请只做分诊，不重写正文：

{{catalog_json}}

输出数组，每项：
{
  "title": "",
  "author": "",
  "status": "school_ok/same_title_conflict/not_school/unknown/duplicate",
  "stage_issue": "",
  "reason": "",
  "confidence": 0.0-1.0
}

规则：
- 同名不同作者：same_title_conflict
- 常见但不在教材：not_school
- 无法确认：unknown
- 同一篇跨学段重复：duplicate，不要直接删
```

## Prompt F：替代来源审计

```text
你是儿童教材来源审计员。

输入：
标题：{{title}}
作者：{{author}}
来源标记：{{source_flag}}
正文：
{{text}}

请输出：
{
  "is_textbook_source": true/false/unknown,
  "is_alternative_source": true/false,
  "risk": "low/medium/high",
  "issues": ["版本差异/同名错配/节选/来源不明/繁简混用"],
  "must_human_review": true/false,
  "summary": ""
}

来源为“非课本获取”时，is_textbook_source 必须为 false，risk 至少 medium。
```

## 推荐执行顺序

```text
1. Prompt B 同名碰撞
2. Prompt A 教材归属
3. Prompt D 学段年级
4. Prompt C 全文逐字比较
5. Prompt F 来源风险
6. 人工对照教材扫描页签字
```

## 汇总字段

```text
title
author
stage
status
same_title_mismatch
is_full_text
wrong_chars
source
verified
reviewer
review_date
```
