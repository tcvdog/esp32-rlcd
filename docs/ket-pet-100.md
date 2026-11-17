# KET / PET 各 100 词 + 200 句覆盖清单

本清单与 ESP32 单词卡固件共用同一份数据（`data/ket_pet_words.tsv`），
由 `tools/gen_word_cards.py` 自动生成，词条和例句不会互相脱节。

## 覆盖情况

- 新增 KET/PET 单词：**200** 个（KET 100 + PET 100）
- 例句：**100** 句，每句都包含对应的目标单词
- 100 个新增词全部被例句覆盖（构建脚本逐条校验）
- 另保留 6 张基础复习卡（apple / banana / cat / dog / book / water），不计入 100

## 用法建议

1. 先读英文单词，自己造一个句子，再按 **A** 看中文释义。
2. 读例句时遮住译文，确认自己理解整句，而不是只认识目标词。
3. 左右键顺序复习；一天 10 词，10 天完成一轮 KET/PET 新增词。
4. 第二轮只看例句，尝试说出目标词的中文意思。

## KET 50 词

| # | 单词 | 中文 | 例句 | 译文 |
|---|---|---|---|---|
| 1 | accept | 接受 | I am happy to accept your invitation to the party. | 我很高兴接受你去聚会的邀请。 |
| 2 | advice | 建议 | My teacher gave me some good advice about my homework. | 老师给了我一些关于作业的好建议。 |
| 3 | afford | 买得起 | We cannot afford a new car this year. | 我们今年买不起新车。 |
| 4 | agree | 同意 | I agree with you about the best way to learn. | 我同意你关于最佳学习方法的看法。 |
| 5 | arrive | 到达 | Please call me when you arrive at the station. | 你到车站时请给我打电话。 |
| 6 | borrow | 借入 | Can I borrow your pen for a minute? | 我能借你的笔用一分钟吗？ |
| 7 | brave | 勇敢的 | The brave boy helped the little girl find her mother. | 那个勇敢的男孩帮小女孩找到了妈妈。 |
| 8 | careful | 小心的 | Be careful when you cross this busy road. | 过这条繁忙的马路时要小心。 |
| 9 | celebrate | 庆祝 | We will celebrate my birthday at a small restaurant. | 我们将在一家小餐馆庆祝我的生日。 |
| 10 | choose | 选择 | You can choose any book from the shelf. | 你可以从书架上选任何一本书。 |
| 11 | climb | 攀登 | They plan to climb the hill before lunch. | 他们计划午饭前爬那座小山。 |
| 12 | collect | 收集 | My brother likes to collect old coins. | 我哥哥喜欢收集旧硬币。 |
| 13 | comfortable | 舒适的 | This chair is very comfortable to sit in. | 这把椅子坐着很舒服。 |
| 14 | complete | 完成 | Please complete the form and give it to me. | 请填完表格然后交给我。 |
| 15 | correct | 正确的 | Your answer is correct, so you get a point. | 你的答案正确，所以得一分。 |
| 16 | dangerous | 危险的 | It is dangerous to swim in this river. | 在这条河里游泳很危险。 |
| 17 | decide | 决定 | We must decide where to go for the holiday. | 我们必须决定假期去哪里。 |
| 18 | describe | 描述 | Can you describe the man you saw last night? | 你能描述一下昨晚你看到的那个人吗？ |
| 19 | difficult | 困难的 | This maths problem is too difficult for me. | 这道数学题对我来说太难了。 |
| 20 | enjoy | 享受 | I really enjoy reading stories before I sleep. | 我很喜欢睡前读故事。 |
| 21 | enough | 足够的 | We have enough food for everyone at the picnic. | 我们的食物够野餐时每个人吃。 |
| 22 | exercise | 锻炼 | Doctors say we should exercise for thirty minutes a day. | 医生说我们每天应该锻炼三十分钟。 |
| 23 | famous | 著名的 | This city is famous for its beautiful gardens. | 这座城市以美丽的园林闻名。 |
| 24 | favourite | 最喜爱的 | Blue is my favourite colour in summer. | 蓝色是我夏天最喜欢的颜色。 |
| 25 | festival | 节日 | The music festival lasts for three days. | 音乐节持续三天。 |
| 26 | finish | 完成 | I will finish my homework before dinner. | 我会在晚饭前完成作业。 |
| 27 | forget | 忘记 | Do not forget to bring your ticket tomorrow. | 明天别忘了带票。 |
| 28 | friendly | 友好的 | Our new neighbour is very friendly to everyone. | 我们的新邻居对每个人都很友好。 |
| 29 | healthy | 健康的 | Eating fruit every day helps you stay healthy. | 每天吃水果有助于保持健康。 |
| 30 | helpful | 有帮助的 | The map was very helpful when we got lost. | 我们迷路时那张地图很有帮助。 |
| 31 | history | 历史 | I am reading a book about the history of China. | 我在读一本关于中国历史的书。 |
| 32 | hobby | 爱好 | Taking photos is my favourite hobby. | 拍照是我最喜欢的爱好。 |
| 33 | invite | 邀请 | I want to invite all my friends to my party. | 我想邀请所有朋友来我的聚会。 |
| 34 | journey | 旅程 | The journey to my grandmother's house takes two hours. | 去我奶奶家的路程要两个小时。 |
| 35 | learn | 学习 | Children learn new words quickly at school. | 孩子们在学校学新单词很快。 |
| 36 | library | 图书馆 | We borrowed three books from the school library. | 我们从学校图书馆借了三本书。 |
| 37 | mistake | 错误 | I made a small mistake in the spelling test. | 我在拼写测试中犯了一个小错误。 |
| 38 | modern | 现代的 | The museum is in a modern building near the park. | 博物馆在公园附近一座现代建筑里。 |
| 39 | nature | 自然 | We should protect nature because it gives us clean air and water. | 我们应该保护自然，因为它给我们干净的空气和水。 |
| 40 | neighbour | 邻居 | Our neighbour helps us look after the garden. | 我们的邻居帮我们照看花园。 |
| 41 | nervous | 紧张的 | She felt nervous before her first piano lesson. | 她上第一节钢琴课前感到紧张。 |
| 42 | offer | 提供 | They offer free water to all the runners. | 他们向所有跑步者提供免费的水。 |
| 43 | popular | 受欢迎的 | Football is a popular sport in many countries. | 足球在许多国家是一项受欢迎的运动。 |
| 44 | prepare | 准备 | I need to prepare dinner for my family tonight. | 今晚我需要为家人准备晚饭。 |
| 45 | remember | 记得 | Please remember to lock the door when you leave. | 离开时请记得锁门。 |
| 46 | repair | 修理 | My father can repair old bicycles in the garage. | 我爸爸能在车库里修理旧自行车。 |
| 47 | simple | 简单的 | The teacher explained the rule in a simple way. | 老师用简单的方式解释了这条规则。 |
| 48 | spend | 花费 | We spend a lot of time in the library. | 我们花很多时间在图书馆里。 |
| 49 | travel | 旅行 | I would like to travel by train next summer. | 明年夏天我想坐火车旅行。 |
| 50 | weather | 天气 | The weather is cold and windy today. | 今天天气寒冷多风。 |
| 51 | abroad | 在国外 | My cousin studies abroad in England. | 我的表弟在英国留学。 |
| 52 | airport | 机场 | We arrived at the airport two hours early. | 我们提前两小时到达了机场。 |
| 53 | answer | 回答 | Please answer the phone for me. | 请帮我接一下电话。 |
| 54 | beach | 海滩 | The children played on the beach all day. | 孩子们在海滩上玩了一整天。 |
| 55 | bicycle | 自行车 | He rides his bicycle to school every day. | 他每天骑自行车上学。 |
| 56 | breakfast | 早餐 | I have breakfast at seven every morning. | 我每天早上七点吃早餐。 |
| 57 | camera | 相机 | She took a photo with her new camera. | 她用新相机拍了一张照片。 |
| 58 | cinema | 电影院 | Let's go to the cinema this weekend. | 这个周末我们去电影院吧。 |
| 59 | clothes | 衣服 | I need to buy some new clothes for winter. | 我需要买些新衣服过冬。 |
| 60 | computer | 电脑 | My computer is very slow today. | 我的电脑今天很慢。 |
| 61 | cook | 烹饪 | My father can cook very well. | 我爸爸很会做饭。 |
| 62 | country | 国家 | China is a very large country. | 中国是一个非常大的国家。 |
| 63 | dance | 跳舞 | She loves to dance at parties. | 她喜欢在聚会上跳舞。 |
| 64 | dinner | 晚餐 | We usually have dinner at six. | 我们通常六点吃晚餐。 |
| 65 | doctor | 医生 | You should see a doctor about that cough. | 你应该去看医生治咳嗽。 |
| 66 | draw | 画画 | He likes to draw pictures of animals. | 他喜欢画动物的图画。 |
| 67 | dream | 梦想 | Her dream is to become a teacher. | 她的梦想是成为一名老师。 |
| 68 | early | 早的 | I woke up early to catch the train. | 我早起去赶火车。 |
| 69 | family | 家庭 | My family goes on holiday every summer. | 我家每年夏天都去度假。 |
| 70 | farm | 农场 | They grow vegetables on a small farm. | 他们在一个小农场种蔬菜。 |
| 71 | film | 电影 | We watched a funny film last night. | 我们昨晚看了一部有趣的电影。 |
| 72 | flower | 花 | She gave her mother a beautiful flower. | 她给妈妈送了一朵漂亮的花。 |
| 73 | food | 食物 | Chinese food is popular around the world. | 中国食物在世界各地都很受欢迎。 |
| 74 | friend | 朋友 | My best friend lives next door. | 我最好的朋友住在隔壁。 |
| 75 | game | 游戏 | Let's play a game after school. | 放学后我们玩个游戏吧。 |
| 76 | garden | 花园 | There are many roses in our garden. | 我们的花园里有很多玫瑰。 |
| 77 | gift | 礼物 | He bought a gift for his sister. | 他给妹妹买了一份礼物。 |
| 78 | grandparent | 祖父母 | My grandparent lives in the countryside. | 我的祖父母住在乡下。 |
| 79 | hospital | 医院 | The hospital is near the train station. | 医院在火车站附近。 |
| 80 | hotel | 酒店 | We stayed at a nice hotel by the sea. | 我们住在海边一家不错的酒店。 |
| 81 | hungry | 饥饿的 | I am hungry after the long walk. | 走了很长的路后我饿了。 |
| 82 | kitchen | 厨房 | Mum is cooking noodles in the kitchen. | 妈妈正在厨房煮面条。 |
| 83 | letter | 信 | I wrote a letter to my pen friend. | 我给我的笔友写了一封信。 |
| 84 | market | 市场 | We buy fresh fruit at the market. | 我们在市场买新鲜水果。 |
| 85 | money | 钱 | He saves money to buy a new bike. | 他存钱想买一辆新自行车。 |
| 86 | mountain | 山 | They climbed the mountain last weekend. | 他们上周末爬了山。 |
| 87 | museum | 博物馆 | The museum is free on Sundays. | 博物馆周日免费。 |
| 88 | music | 音乐 | She listens to music while studying. | 她学习时听音乐。 |
| 89 | park | 公园 | We often walk in the park after dinner. | 我们晚饭后经常在公园散步。 |
| 90 | party | 聚会 | I am going to a birthday party tonight. | 我今晚要去参加一个生日聚会。 |
| 91 | photo | 照片 | Can you take a photo of us? | 你能给我们拍张照片吗？ |
| 92 | picture | 图画 | There is a picture on the wall. | 墙上有一幅画。 |
| 93 | plane | 飞机 | The plane leaves at nine o'clock. | 飞机九点起飞。 |
| 94 | restaurant | 餐馆 | We had lunch at a Chinese restaurant. | 我们在一家中餐馆吃了午饭。 |
| 95 | school | 学校 | Our school has a big playground. | 我们学校有一个大操场。 |
| 96 | shop | 商店 | The shop closes at eight in the evening. | 这家商店晚上八点关门。 |
| 97 | station | 车站 | The bus station is across the road. | 公交车站就在马路对面。 |
| 98 | street | 街道 | Our street is quiet in the morning. | 我们这条街早上很安静。 |
| 99 | ticket | 票 | I bought two tickets for the show. | 我买了两张演出的票。 |
| 100 | vegetable | 蔬菜 | You should eat more vegetables every day. | 你每天应该多吃蔬菜。 |

## PET 50 词

| # | 单词 | 中文 | 例句 | 译文 |
|---|---|---|---|---|
| 51 | achieve | 实现 | She worked hard to achieve her goal of winning the race. | 她努力实现赢得比赛的目标。 |
| 52 | allow | 允许 | Our school does not allow students to use phones in class. | 我们学校不允许学生在课堂上使用手机。 |
| 53 | amazing | 令人惊叹的 | The view from the top of the mountain was amazing. | 山顶的景色令人惊叹。 |
| 54 | ancient | 古代的 | We visited an ancient temple during our trip. | 我们旅行期间参观了一座古庙。 |
| 55 | appear | 出现 | A rainbow began to appear after the heavy rain. | 大雨过后开始出现彩虹。 |
| 56 | attend | 参加 | More than fifty people will attend the meeting tomorrow. | 明天将有五十多人参加会议。 |
| 57 | avoid | 避免 | Try to avoid using your phone late at night. | 尽量避免深夜使用手机。 |
| 58 | brilliant | 出色的 | Your idea for the school show is brilliant. | 你为学校演出出的主意很出色。 |
| 59 | challenge | 挑战 | Learning to swim was a big challenge for me. | 学游泳对我来说是个很大的挑战。 |
| 60 | communicate | 交流 | We communicate with our teacher by email. | 我们通过电子邮件与老师交流。 |
| 61 | compare | 比较 | If you compare the two photos, you will see the difference. | 如果你比较这两张照片，就会发现不同。 |
| 62 | confident | 自信的 | She feels confident when she speaks English in public. | 她在公共场合说英语时感到自信。 |
| 63 | continue | 继续 | The rain will continue until tomorrow morning. | 雨将持续到明天早上。 |
| 64 | convenient | 方便的 | The new bus stop is very convenient for our street. | 新公交站对我们这条街很方便。 |
| 65 | create | 创造 | The students want to create a poster about saving water. | 学生们想制作一张关于节约用水的海报。 |
| 66 | curious | 好奇的 | The curious child asked many questions about the stars. | 那个好奇的孩子问了许多关于星星的问题。 |
| 67 | develop | 发展 | Reading every day helps children develop their language skills. | 每天阅读有助于孩子发展语言能力。 |
| 68 | difference | 差别 | There is a big difference between the two answers. | 这两个答案之间有很大差别。 |
| 69 | discover | 发现 | We were excited to discover a small coffee shop near the beach. | 我们很兴奋地发现海滩附近有一家小咖啡馆。 |
| 70 | discuss | 讨论 | Let's discuss the plan before we start working. | 我们开始工作前先讨论一下计划吧。 |
| 71 | education | 教育 | Every child should have a good education. | 每个孩子都应该接受良好的教育。 |
| 72 | encourage | 鼓励 | My parents encourage me to try new things. | 父母鼓励我尝试新事物。 |
| 73 | energy | 能量 | We get energy from the food we eat every day. | 我们从每天吃的食物中获得能量。 |
| 74 | environment | 环境 | Recycling paper is good for the environment. | 回收纸张对环境有益。 |
| 75 | especially | 尤其 | I love fruit, especially oranges and grapes. | 我喜欢水果，尤其是橙子和葡萄。 |
| 76 | excellent | 优秀的 | She did an excellent job on the science project. | 她在科学项目上做得非常出色。 |
| 77 | expect | 期待 | We expect to arrive at the hotel before dark. | 我们预计天黑前到达酒店。 |
| 78 | experience | 经历 | Living in another country was a wonderful experience. | 在另一个国家生活是一次美妙的经历。 |
| 79 | explain | 解释 | Could you explain this word to me again? | 你能再给我解释一下这个词吗？ |
| 80 | fortunate | 幸运的 | We were fortunate to get the last two tickets. | 我们很幸运买到了最后两张票。 |
| 81 | future | 未来 | In the future, more people may work from home. | 未来可能有更多人在家工作。 |
| 82 | gather | 聚集 | People gather in the square to watch the fireworks. | 人们聚集在广场上看烟花。 |
| 83 | generous | 慷慨的 | It was generous of you to share your lunch with me. | 你和我分享午餐真是慷慨。 |
| 84 | government | 政府 | The government plans to build a new hospital here. | 政府计划在这里建一家新医院。 |
| 85 | habit | 习惯 | Drinking water in the morning is a healthy habit. | 早上喝水是一个健康的习惯。 |
| 86 | imagine | 想象 | Can you imagine life without the internet? | 你能想象没有互联网的生活吗？ |
| 87 | improve | 提高 | I want to improve my English before the exam. | 我想在考试前提高我的英语。 |
| 88 | include | 包括 | The price will include breakfast and free Wi-Fi. | 价格将包括早餐和免费 Wi-Fi。 |
| 89 | information | 信息 | You can find more information on the school website. | 你可以在学校网站上找到更多信息。 |
| 90 | instead | 代替 | We stayed at home instead of going to the cinema. | 我们待在家里，而不是去电影院。 |
| 91 | interesting | 有趣的 | The history lesson was more interesting than I expected. | 历史课比我预想的更有趣。 |
| 92 | knowledge | 知识 | Reading gives us knowledge about the world. | 阅读给我们关于世界的知识。 |
| 93 | language | 语言 | English is a useful language for travel and work. | 英语是旅行和工作中一门有用的语言。 |
| 94 | manage | 设法做到 | How do you manage to study and work at the same time? | 你怎样设法同时学习和工作？ |
| 95 | opportunity | 机会 | This job is a great opportunity to learn new skills. | 这份工作是学习新技能的好机会。 |
| 96 | organize | 组织 | Our class will organize a book sale for charity. | 我们班将为慈善组织一次图书义卖。 |
| 97 | patient | 耐心的 | A good teacher is patient with every student. | 好老师对每个学生都有耐心。 |
| 98 | polite | 礼貌的 | It is polite to say thank you when someone helps you. | 别人帮你时说声谢谢是有礼貌的。 |
| 99 | pretend | 假装 | The children like to pretend they are pirates. | 孩子们喜欢假装自己是海盗。 |
| 100 | probably | 很可能 | It will probably rain this afternoon, so take an umbrella. | 今天下午很可能会下雨，所以带把伞。 |
| 101 | ability | 能力 | She has the ability to solve difficult problems quickly. | 她有快速解决难题的能力。 |
| 102 | active | 活跃的 | My brother is very active and plays sports every day. | 我哥哥很活跃，每天都做运动。 |
| 103 | adventure | 冒险 | Our trip to the mountains was a great adventure. | 我们去山里的旅行是一次很棒的冒险。 |
| 104 | ambition | 抱负 | Her ambition is to become a famous scientist. | 她的抱负是成为一名著名的科学家。 |
| 105 | apologize | 道歉 | You should apologize to your friend for being late. | 你应该为迟到向你的朋友道歉。 |
| 106 | appreciate | 感激 | I really appreciate your help with my homework. | 我非常感激你帮我做作业。 |
| 107 | attitude | 态度 | A positive attitude helps you learn faster. | 积极的态度能帮助你学得更快。 |
| 108 | average | 平均的 | The average score in our class was quite high. | 我们班的平均分相当高。 |
| 109 | balance | 平衡 | It is important to balance study and rest. | 平衡学习和休息很重要。 |
| 110 | benefit | 好处 | Regular exercise has many benefits for your health. | 经常锻炼对你的健康有很多好处。 |
| 111 | career | 职业 | He wants a career in medicine after university. | 他大学毕业后想从事医学职业。 |
| 112 | celebrity | 名人 | The celebrity visited our town to open a new shop. | 那位名人来我们镇为新店开业。 |
| 113 | ceremony | 典礼 | The school ceremony started at nine in the morning. | 学校的典礼在早上九点开始。 |
| 114 | character | 性格 | My best friend has a kind and honest character. | 我最好的朋友有善良诚实的性格。 |
| 115 | charity | 慈善 | We raised money for a local charity last week. | 我们上周为当地一家慈善机构筹款。 |
| 116 | comfort | 安慰 | She tried to comfort her sister after the exam. | 考试后她试图安慰她的妹妹。 |
| 117 | community | 社区 | Our community holds a clean-up day every spring. | 我们社区每年春天举办一次清洁日。 |
| 118 | competition | 比赛 | He won first prize in the singing competition. | 他在歌唱比赛中获得了一等奖。 |
| 119 | creative | 有创造力的 | She is a creative student who loves art and music. | 她是一个热爱艺术和音乐的有创造力的学生。 |
| 120 | connection | 联系 | There is a strong connection between diet and health. | 饮食和健康之间有很强的联系。 |
| 121 | consider | 考虑 | We should consider taking the train instead of the bus. | 我们应该考虑坐火车而不是公共汽车。 |
| 122 | courage | 勇气 | It takes courage to speak in front of the whole school. | 在全校面前讲话需要勇气。 |
| 123 | culture | 文化 | Learning a language helps you understand another culture. | 学习一门语言能帮助你了解另一种文化。 |
| 124 | custom | 习俗 | Giving red packets is a popular custom in China. | 发红包在中国是一种流行的习俗。 |
| 125 | decision | 决定 | Choosing a university is an important decision. | 选择大学是一个重要的决定。 |
| 126 | decorate | 装饰 | We decorate the classroom for the new year party. | 我们为新年派对装饰教室。 |
| 127 | delicious | 美味的 | The noodles at that small restaurant are delicious. | 那家小餐馆的面条很美味。 |
| 128 | depend | 依靠 | Whether we go out depends on the weather tomorrow. | 我们是否出去取决于明天的天气。 |
| 129 | design | 设计 | She wants to design clothes for a big company. | 她想为一家大公司设计服装。 |
| 130 | detail | 细节 | Please explain the plan to me in more detail. | 请更详细地向我解释这个计划。 |
| 131 | direction | 方向 | We asked a stranger for directions to the station. | 我们向一个陌生人问去车站的方向。 |
| 132 | distance | 距离 | The distance from my home to school is two kilometres. | 从我家到学校的距离是两公里。 |
| 133 | effort | 努力 | With more effort, you can pass the exam easily. | 付出更多努力，你就能轻松通过考试。 |
| 134 | electricity | 电 | We should save electricity by turning off the lights. | 我们应该关灯来节约用电。 |
| 135 | emergency | 紧急情况 | Call this number in case of an emergency. | 遇到紧急情况请拨打这个号码。 |
| 136 | event | 活动 | The sports event will take place next Saturday. | 体育赛事将在下周六举行。 |
| 137 | exchange | 交换 | I want to exchange this shirt for a larger size. | 我想把这件衬衫换成大一号的。 |
| 138 | fashion | 时尚 | She is very interested in fashion and design. | 她对时尚和设计非常感兴趣。 |
| 139 | fever | 发烧 | He stayed home because he had a high fever. | 他因为发高烧待在家里。 |
| 140 | freedom | 自由 | Children need freedom to play and explore. | 孩子们需要玩耍和探索的自由。 |
| 141 | goal | 目标 | My goal is to read ten English books this year. | 我的目标是今年读十本英语书。 |
| 142 | graduate | 毕业 | My sister will graduate from university next June. | 我姐姐明年六月将大学毕业。 |
| 143 | guest | 客人 | We had ten guests at our dinner party. | 我们的晚宴有十位客人。 |
| 144 | guide | 导游 | The guide showed us around the old city. | 导游带我们参观了老城区。 |
| 145 | honest | 诚实的 | An honest person always tells the truth. | 诚实的人总是说真话。 |
| 146 | independent | 独立的 | Living alone made her more independent. | 独自生活使她变得更加独立。 |
| 147 | industry | 工业 | The car industry is important in this city. | 汽车工业在这座城市很重要。 |
| 148 | influence | 影响 | Friends can have a strong influence on your choices. | 朋友对你的选择有很大影响。 |
| 149 | interview | 面试 | She has a job interview on Monday morning. | 她周一上午有一场工作面试。 |
| 150 | invention | 发明 | The telephone was an important invention. | 电话是一项重要的发明。 |

## 基础复习卡（6）

| 单词 | 中文 | 例句 | 译文 |
|---|---|---|---|
| apple | 苹果 | I eat an apple every morning before school. | 我每天早上上学前吃一个苹果。 |
| banana | 香蕉 | The monkey is eating a banana in the tree. | 猴子正在树上吃香蕉。 |
| cat | 猫 | My cat likes to sleep in the sun. | 我的猫喜欢在阳光下睡觉。 |
| dog | 狗 | The dog runs to the door when I come home. | 我回家时狗会跑向门口。 |
| book | 书 | This book has many funny stories for children. | 这本书里有很多给孩子的有趣故事。 |
| water | 水 | Please drink some water after your run. | 跑步后请喝点水。 |

---

数据源：`data/ket_pet_words.tsv`；固件表：`game_proto/main/word_cards.h`。
