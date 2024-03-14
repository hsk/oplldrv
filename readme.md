# テンポ指定とqコマンドが使える簡単なOPLLドライバを作り高速化とデータ圧縮を行う

テンポとqコマンドをコンパイラが処理する最小限の機能を備えたYM2416サウンドドライバとコンパイラを開発します。開発は独自エミュレータ上動くバイナリファイルを作って実行する形で行い、コンパイラはpython3で書き、サウンドドライバはC言語で書いてSDCCでコンパイルしてZ80のコードとして動きます。独自開発のエミュレータはコマンドライン上で実行するとwavファイルをすぐに生成するため開発が楽に行えます。また、クロック単位の高精度なベンチマークとパーセント表示で高速化を支援します。ドライバはC言語をリファレンス実装として書き、アセンブラ結果を参考にインラインアセンブラで高速化なバージョンを作ります。コンパイラはpython3で正規表現やパターンマッチング機能を使って複数パスで書きます。パーサは構文解析とオクターブや長さの処理を行いjsonの内部コードを生成し、意味解析は内部コードからバイトコードを出力します。

MML構文

```
mml  ::= (head|fm|sound)*       音楽データ
head ::=                        ヘッダ情報
       | #tempo I               テンポ 1分間に４部音符が何回鳴らせるか？
       | #opll_mode I           I=0: 通常モード I=1:リズムモード
       | #title {"[^"]*"}       タイトル
fm   ::= @vI={I1,I2,I3,...,I24} 音色データ
line ::= [9ABCDEFGH]+ cmd*      1行データ
I    ::= [0-9]+                 数値
L    ::= I(^I)*                 長さ
cmd  ::=                        コマンド
       | o I                    オクターブを指定します。
       | >                      オクターブを１つ上げます。
       | <                      オクターブを１つ下げます。
       | [cdefgab][+-]?L?       トーンを指定します。
       | [bsmch]+[L:]           ドラムを演奏します。
       | r L?                   休符を指定します。
       | l L                    デフォルト長を指定します。
       | v I                    ボリュームを0-15で指定します。
       | t I                    テンポを60秒に4分音符が何回鳴らせるかで指定します。
       | q I                    ポルタメントを1-8で指定します。1で短く8だとなり続けます。
       | @ I                    音色を指定します。FM音源を使うので音色指定はできます。
       | [ cmd* ]I              ループします。
       | so                     サスティンをオンにします
       | &                      音と音をつなげます
```

MMLの構文を上に示します。MMLのデータは行データの集まりです。行データはチャンネルに対応し行のはじめにD E F G のうちどれかを書いてチャンネルを選択しその後にコマンドを連続して書きます。音階を表す cdefgab o<> とその他の rlvtq@ のコマンドが使えます。

コンパイラ内部構文

```
ir ::= i*                    内部コードリスト
i  ::=                       内部コード
     | ["t",  n]             テンポ
     | ["tone", "r", n]      ウエイト
     | ["v", n]              ボリューム
     | ["tone", n, n]        トーン
     | ["@", n]              音色
     | ["["]                 ループ開始
     | ["]", n]              N回ループ
     | ["l", n]              音符長
     | ["<"]                 オクターブ+1
     | [">"]                 オクターブ-1
     | ["q", n]              ポルタメント 1-8
     | ["v-",n]              音量+
     | ["v+",n]              音量-
     | ["&"]                 スラー(仮実装)
     | ["|"]                 ブレイク(未実装)
     | ["drum", n, n]        ドラム
     | ["drum_v", n, n, n]   ドラム音量
     | ["so"]                サスティンオン
```

構文解析によって出力される内部コードを上に示します。長さやオクターブ、ポルタメントはパーサ側で処理していたため内部コードに含まれていませんでしたが、パーサ変更に伴って意味解析で処理するようになり含まれるようになりました。

バイトコード構文

```
bytes   ::= byte*
byte    ::=
          | PKEYOFF b   キーオフ ウェイト時間を0-255 0は256の意味になります
          | PWAIT   b   ウェイト指定 ウェイト時間を0-255 0は256の意味になります
          | PTONE       PTONE自体でトーン指定0-95 ウェイト時間0-255 0は256の意味になります
          | PDRUM       PDRUM & 0x3f をドラム指定として0x0eレジスタに設定します。 
          | PVOLUME b   音色0-15 << 4 + 音量 0-15
          | PEND        終了
          | PLOOP   b   ループ開始、ループ回数 0-255 0は256の意味になります
          | PNEXT   bb  ループ終了 ループ開始位置オフセット 0-65535
          | PBREAK  bb  ループブレイク ループ終了位置位置オフセット 0-65535
          | PSLOAD  b   サウンドデータロード 0,8,16,...,240
          | PSLAON      スラーをONにする
          | PSUSON      サスティンをONにする
PTONE   ::= 0x00-0x5f
PDRUM   ::= 0x60-0x7f
PKEYOFF ::= 0x80
PWAIT   ::= 0x81
PVOLUME ::= 0x82
PEND    ::= 0x83
PLOOP   ::= 0x84
PNEXT   ::= 0x85
PBREAK  ::= 0x86
PSLOAD  ::= 0x87
PSLAON  ::= 0x88
PSUSON  ::= 0x89
b       ::= 0-255
```

バイトコードを上に示します。タグに続きいくつかデータが続いたバイトコードが複数存在します。バイトコードはキーオフとウェイト、トーン、音色音量、終了の５つのコードからなります。音量と音色は１つのコードで表されることに注意してください。トーンデータはバイトデータから表をドライバが引くので１バイトで済ませることができ、更にタグもなくしてデータ削減されています。

## コンパイラの実装

ドライバ側でテンポ処理をするとなると掛け算を行わなければならなくなりそうなのでコンパイラ側でやったほうがよさそうです。テンポをコンパイラ側でうまく吸収し、ポルタメントの処理も行いたい。テンポ処理を行う簡潔な処理系を参考により簡潔なプログラムを組みたい。そこで我々は hra さんの bgm_driver https://github.com/hra1129/bgm_driver を参考にすることにしました。bgm_driver はC言語のワンパスコンパイラでコンパイルしZ80のアセンブラで書かれたMSXのPSG用のドライバです。テンポ管理は浮動小数点数で行われており、ポルタメントは音符の長さを８で割って書けるだけになってました。これは簡単です。テンポは`60*60/(tempo/4)` と書かれておりました。我々はより高レイヤーのpythonで複数パスに分けてコンパイラを開発することによりより理解しやすいものができるのではないかと考え開発しました。ワンパスコンパイラは１人カンバン方式のようなやり方をしているため看板を付けて持ち歩く手間がなく極めて高速に動作するので素晴らしい技術です。しかしながら我々には現代的な高速なコンピュータと高機能な言語処理系でコンパイルが可能です。pythonはMSX3の標準言語として使うという話もあるのでpythonで開発することを選択しました。

## ループについて

ネストしたループをもつには内部状態にスタックをもつ必要があります。スタックはコンテキストごとに必要な深さだけ用意する必要があります。スタックサイズをコンパイラが数えてサイズを予め求めておくことで最小限のデータサイズでループが実現できます。コンパイラにはループのネストを数える機能をもたせましょう。

コンパイラはループ開始位置でスタックにその位置を記録し、ループ開始タグとループ回数領域を１バイト開けて次に進みます。スタックサイズの最大値も更新します。ループ終了を発見したらスタックからその位置を取得してループ回数を書き戻します。ループ終了タグと開始位置を登録して次に進みます。コンパイル結果には各データに必要なスタックサイズをチャンネル数分記録しておきます。スタックにはループ開始時の時間も記録し、終了時にはループ回数分を経過時間に追加します。ズレが生じた場合はウェイトを加えて調整します。

ドライバは起動時にスタックサイズからスタックポインタのコンテキストにスタックを割り付けます。ループ開始位置ではスタックポインタを進めて回数を記録します。ループ終了位置ではスタックから回数デクリメントしてゼロでないなら開始位置に移動します。ゼロならポインタを２つ進めます。

コンパイラに実装し、データを変えてループしたデータを作ります。ドライバをC言語で実装します。ドライバをアセンブラで実装します。データを計測します。

ブレイクは、最後のループ時に後続の命令を飛ばしてループから抜ける命令です。ブレイク命令はジャンプアドレスを２バイト持つ命令で、コンパイラはスタック内にジャンプアドレスを書き込んでおきます。ループ終了時にコンパイラは、ブレイク命令との相対アドレスを計算して飛び先がループの外になるようにデータを設定します。ドライバはブレイク命令時にループ変数をチェックし最後のループなら抜けますが、最後でないなら２だけ進めて次に進みます。

※ブレイクを作る際に忘れてはならないのはループを抜けるときにスタックを戻すことと、トータル時間の算出にブレークの時間を考慮にいれることです。忘れないようにしましょう。(実装時に忘れていて悩みました。)ブレークがあった場合はトータル時間算出時にループが１回少ないことにして、ブレークまでの時間を足してから、スタックに積むときにループ開始時の値を引いて保存しておいて加えます。if文が２回あるといいでしょう。


## 高精度なテンポ調整

64分音符を使ったループなどで差分が十分に吸収できないケースがあります。
ループでテンポ調整をする場合は8回で5速くなっている場合は10110110のような感じでウェイトを加えれば大きなずれが生じなくなります。
しかしながら遅れが生じている場合はウェイトを加える調整ができません。
そこでまず、速めに倒す形で調整するようにしました。
次に、ループで生じるクロックのずれをループ時に１クロックずつ解消するためDDAのアルゴリズムで行うことにしました。
データ上にはまずループ開始位置でDDA計算の初期値を加えました。また、ループ終了位置にDDA計算のずれの値とループ回数を加えました。
１つのループに付き３バイト増えることになりました。
コンパイラは時間の差分をもつことをやめて、1/60秒単位の時間allと浮動小数点数の時間all2で管理するようにしました。
ウェイトを書き込む際はall2とallの差分の整数部分のみを加えて1/60秒単位で書き込みます。
常に速くなるようになることを確認したら次に、ループ時のずれを1/60秒単位で求め、ズレの分を書き込みます。

ドライバはまずスタックサイズを２倍にしてDDA計算領域を用意します。スタックのトップはウェイトようにするため、手前に入れることにしました。
ループ開始時にはループ数をスタックに加えた後DDAの初期値もスタックに加えます。初期値を常にスタック上に持てばデータサイズは１つ減らせるでしょうが今回は持たせませんでした。
ループ終了時はDDAのずれを取り出し、スタックに加え、しきい値を超えていた場合はしきい値をスタックから差し引いた後関数からリターンします。
DDAの計算はループブレイクの処理にも加えます。

このようにすることで、ループでズレが大きく膨らむ問題が解消できます。

## オリジナル音色の変更について

```mml
@vI={I1,I2,I3,...,I24}
```

の形で音色を設定できて @I の形でロードして用います。

コンパイラはまずオリジナル音色のデータをレジスタ登録用に変換して配列に出力します。@15移行の参照があれば、(NO-15)*8のデータを加えてPSLOAD命令を出力し、音色の参照はそれ移行、0番を参照するようにします。
ロードされている音色は覚えておいてほかからの参照があった場合は、その値を用いるように最適化するとより良いでしょう。

ドライバは初期化時にサウンドデータをコンテキストに登録しておきます。
PSLOAD I 命令がサウンドデータのコンテキストにIを加えたアドレスからデータをOPLLに登録します。Iの値は番号*8になっており8バイトx32個まで使えます。

## リズム音源について

リズム音源を使うにはリズムモードをONにするひつようがあります。レジスタ0Eの5ビット目を1にするとリズム音モードになります。
リズム音モードにした場合は基本的にいかのレジスタ設定を行ってから使います。

```
ym2413(0x0e, 1<<5);
ym2413(0x16, 0x20);// F-Num LSB for channel 7 (slots 13,16)  BD1,BD2
ym2413(0x17, 0x50);// F-Num LSB for channel 8 (slots 14,17)  HH ,SD
ym2413(0x18, 0xC0);// F-Num LSB for channel 9 (slots 15,18)　TOM,TCY 
ym2413(0x26, 0x05);// Block/F-Num MSB for channel 7          BD1,BD2
ym2413(0x27, 0x05);// Block/F-Num MSB for channel 8          HH ,SD
ym2413(0x28, 0x01);// Block/F-Num MSB for channel 9          TOM,TCY
```

リズム音源を鳴らすにはレジスタ0x0eの0-5ビットをOFFからONにする必要があります。ONからONではキックされません。音楽データは2バイト目のデータが１ならリズム音モード、０なら通常モードになります。リズム音再生時は音程の代わりに0x60-0x7Fのデータで演奏し、0x3fとandを取って演奏します。リズム音の音量設定も処理が変わるポイントですが、最初のバージョンでは音量設定に対応しません。

コンパイラはドラムのデータを元にPDRUM(0x60-0x7f)を発行します。混乱するので、すべての登録されたチャンネルを出力するようにもどしました。

## 状態変更ループの展開

[a))]4 のようなループは音量を次から次へと変える効果を出せます。しかし状態はコンパイラで吸収するような設計であるため現状は音量はループ開始位置で元の音量に戻ってしまいます。
そこで、音量状態が変わるようなループを検出して展開することにしました。
無限ループも変えたほうが良さそうですから、ループ展開したいところです。
そこで、ループ展開はコンパイラの別パスとして作ることにします。
ループ展開パスでは全データをトラバースしてループ開始時と終了時の状態を比較して、違う場合はループ回数分展開します。データサイズが大きくなってしまう問題はありますが、ドライバに変更が不要なのでやってみましょう。
パスを分けることで、ループ展開時には他の問題と切り離して考えることができます。

## ドライバの実装

- v0.1.0
     - データサイズは519bytesです。
     - C言語とアセンブラの実装をデファイン値で変更して比較できるようにしました。
     - コンテキストをループする処理に掛け算が含まれるので激重なのでポインタに加える形にすると速くなります。
- v0.2.0
     - データサイズは450bytesに減りました。v0.1.0のサイズの87%のサイズになりました。
     - トーンテーブルをドライバに持たせトーンデータを１バイトにしました。
     - ウェイト時は dec (hl) $ ret nz $ inc (hl) とすることですぐに抜けることで大幅に高速化できました。
     - 終了時はウェイトを最大にして抜けるとウェイトの負荷がさげられます。
     - トーンのあとは必ずウェイトが入るからウェイト処理をしてすぐ抜けるようにしました。
- v0.3.0
     - データサイズは342bytesに減りました。スタックサイズ４バイト増えましたがループで342/450=76%に圧縮できました。
     - ループコマンドを追加しました。
- v0.4.0
     - データサイズが285bytesに減りました。ヘッダの命令を消しトーン後のウェイトコマンドをトーンに統合したことによります。
- v0.5.0
     - データサイズが254bytesに減りました。トーンデータのタグをなくしたことによります。
     - コンパイラの作りを９音対応にして、ファイル読み込みに対応しました。
- v0.6.0
     - パーサから長さ以外の状態を排除しました。長さは状態を残さないと面倒なので残した形。9から始まるFM音源に対応するための改修準備です。
- v0.7.0
     - パーサを以前作ったものに入れ替え、それに伴ってテストデータ変更を変更しました。
- v0.8.0
     - ループブレイクの追加。
- v0.9.0
     - オリジナル音色の追加。
- v1.0.0
     - リズム音源の追加。
- v1.1.0
     - 高精度なテンポ調整を行うようにしました。
- v1.2.0
     - 状態を変更するループは展開することにしました。
- v1.3.0
     - サスティンとスラーを追加しました。
- v1.4.0
     - ドラムの音量設定に対応しました。

## 計測結果

C言語による実装はテストデータに対して519バイトのデータを出力し、CPU使用率が5.113%になりました。アセンブラによって最適化した実装はCPU使用率が2.887% になりました。アセンブラによる実装で1.77倍速くなりました。C言語の外側のループが遅いので最適化してみると 2.564% > 2.433% と変化し 1.99倍、2.10倍にできました。もっと速くなってもいいと思いますが今後の課題とします。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v0.1.0  | 5.113% | 1.00  | c言語       |
| v0.1.0a | 2.887% | 1.77  | アセンブラ   |
| v0.1.0b | 2.564% | 1.99  | ループ最適化  |
| v0.1.0c | 2.433% | 2.10  | ループ最適化2 |

v0.2.0 では C言語による実装が 5.522%,アセンブラとその他の高速化で1.153%と4.79倍速くなりました。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v0.2.0  | 5.522% | 1.00  | c言語       |
| v0.2.0a | 1.681% | 3.28  | アセンブラ   |
| v0.2.0b | 1.329% | 4.16  | ループ最適化  |
| v0.2.0c | 1.153% | 4.79  | ループ最適化2 |

C言語は機能追加とアセンブラ追従による減速が生じているのに対して、アセンブラは如実に高速化の影響が出ました。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v0.3.0  | 3.760% | 1.00  | c言語       |
| v0.3.0a | 1.691% | 2.22  | アセンブラ   |
| v0.3.0b | 1.340% | 2.81  | ループ最適化  |
| v0.3.0c | 1.164% | 3.23  | ループ最適化2 |

C言語の結果が早くなったのはおそらく７倍するより９倍するほうが計算が早いためでしょう。
v0.2.0よりも若干0.011%ほど遅くなりましたがほとんど変わらず、アセンブラ化では３倍速い結果となりました。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v0.4.0  | 3.653% | 1.00  | c言語       |
| v0.4.0a | 1.687% | 2.17  | アセンブラ   |
| v0.4.0b | 1.336% | 2.73  | ループ最適化  |
| v0.4.0c | 1.160% | 3.15  | ループ最適化2 |

データ縮小に伴い、全体的に若干速くなりました。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v0.5.0  | 3.665% | 1.00  | c言語       |
| v0.5.0a | 1.685% | 2.18  | アセンブラ   |
| v0.5.0b | 1.334% | 2.75  | ループ最適化  |
| v0.5.0c | 1.154% | 3.18  | ループ最適化2 |

データ縮小に伴い、C言語は若干遅くなりましたがアセンブラの処理は読み込みがなくなった分若干速くなりました。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v0.8.0  | 3.683% | 1.00  | c言語       |
| v0.8.0a | 1.692% | 2.18  | アセンブラ   |
| v0.8.0b | 1.341% | 2.75  | ループ最適化  |
| v0.8.0c | 1.165% | 3.16  | ループ最適化2 |

ブレイクの分岐判定が入った分だけ若干遅くなりましたが微々たるものです。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v0.9.0  | 2.840% | 1.00  | c言語       |
| v0.9.0a | 1.303% | 2.18  | アセンブラ   |
| v0.9.0b | 1.169% | 2.43  | ループ最適化  |
| v0.9.0c | 0.981% | 2.90  | ループ最適化2 |

オリジナル音が入ったので遅くなっているはずですが、チャンネル数分しか音を鳴らしていないのでC言語が速く鳴りその他も高速化しているように見えます。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v1.0.0  | 7.254% | 1.00  | c言語       |
| v1.0.0a | 3.143% | 2.31  | アセンブラ   |
| v1.0.0b | 2.707% | 2.68  | ループ最適化  |
| v1.0.0c | 2.196% | 3.30  | ループ最適化2 |

ドラムの処理が一般的な音源でも足を引っ張った形です。ドラム使ってないけどチェックが入っているのでどうしても遅くなります。
レコード数が９個に増えたのも遅くなる原因でしょう。その分アセンブラでの最適化の割合が増えてます。やはり高速な処理をするならドラムと一般の楽器は分け、必要チャンネルだけ演奏するとよいです。

## 考察

8bit時代のゲームプログラミングにおいてはどうしてもアセンブラによる最適化が必要になります。しかしながらアセンブラだけで書かれたサウンドドライバを理解するのは難しいことです。C言語でその設計を理解してからアセンブラで書くことで理解がより容易になります。コンパイラ側でテンポを調整すればポルタメントの処理もスムーズにできるはずなので実装してみました。テンポが変化するドライバをC言語で作れたのでアセンブラで最適化して２倍の速度に最適化できました。

テンポとポルタメントを実装する技術を理解することができました。
大規模なアセンブラでの開発をC言語のコンパイル結果を元に動かしながら実装する手法は小さなプログラムでは有効でした。しかしそれなりの規模になると状態数が爆発するので書き換えは困難になり失敗しました。
小さなプログラムをいちからアセンブラで書けばつらい状況にはなりませんが動きを理解するのは難しくなります。そこでC言語によるリファレンス実装をコメントとして書いておき実装することでなんとか実装できました。
最初の実装時は初心者レベルでしたのでケアレスミスで長い時間悩みました。この状況を変えるには同じプログラムを何度も書いてみる手法が使えます。
最初は初心者特有のミスが多く発生するため多くの時間がかかってしまいます。動くプログラムとの差分を見ればミスの箇所をすぐに見つけられます。
何度も同じプログラムを書いていると次第に覚えるのでミスが減りますし、ミスをしてもどこでミスをしていそうかがわかってくるのですぐにバグに気がつけるようになります。
１５分、１０分、８分、４分と実装時間が十分短くできるようになるとそれ以上はスピードアップが無理というレベルになりました。
プログラムを修正すると混乱してしまいますがノウハウが貯まればアセンブラネイティブに近づけるはずです。

## メモ

- ブランチの一覧
     - git branch -a
- ブランチに切り替え
     - git checkout main
     - git checkout v\*.\*.\*
- ブランチを作成
     - git checkout -b v\*.\*.\*
- ブランチをリモートに登録
     - git push -u origin v\*.\*.\*
- リモートブランチからローカルブランチを作成
     - git checkout -b v\*.\*.\* origin/v\*.\*.\*
- main にマージ
     - git checkout main
     - git merge v\*.\*.\*
     - git checkout v\*.\*.\*
- ローカルブランチ削除
     - git branch -d v\*.\*.\*
- リモートブランチを削除
     - git push origin --delete v\*.\*.\*
- diff を見る
     - https://github.com/hsk/oplldrv/compare/v0.1.0..v0.2.0
     - https://github.com/hsk/oplldrv/compare/v0.2.0..v0.3.0
     - https://github.com/hsk/oplldrv/compare/v0.3.0..v0.4.0
     - https://github.com/hsk/oplldrv/compare/v0.4.0..v0.5.0
     - https://github.com/hsk/oplldrv/compare/v0.5.0..v0.6.0
     - https://github.com/hsk/oplldrv/compare/v0.6.0..v0.7.0
     - https://github.com/hsk/oplldrv/compare/v0.7.0..v0.8.0
     - https://github.com/hsk/oplldrv/compare/v0.8.0..v0.9.0
     - https://github.com/hsk/oplldrv/compare/v0.9.0..v1.0.0
     - https://github.com/hsk/oplldrv/compare/v1.0.0..v1.1.0
     - https://github.com/hsk/oplldrv/compare/v1.1.0..v1.2.0
     - https://github.com/hsk/oplldrv/compare/v1.2.0..v1.3.0
     - https://github.com/hsk/oplldrv/compare/v1.3.0..v1.4.0
