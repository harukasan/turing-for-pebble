/** @jsxImportSource react */
import { styles as s } from "../styles";

export function Explanation() {
  return (
    <>
      <section className={s.section} aria-labelledby="about-title">
        <h2 className={s.heading} id="about-title">
          watchface について
        </h2>
        <p className={s.prose}>
          反応拡散で育つ模様を、Pebble Time 2 の時計表示に使う watchface
          です。模様の上に時刻と日付を重ね、数字の周りでは模様を薄くして文字を読みやすくしています。ここでは配色やフォント、計算方式を切り替えて見え方を試せます。
        </p>
        <p className={s.prose}>
          パラメータは現在の模様に次のステップから反映されます。モデル、方式、シード、プリセットを変えると初期化します。同じ条件で比較するときは、同じシードとステップ数を使ってください。
        </p>
        <p className={s.prose}>
          <a
            className={s.link}
            href="https://www.karlsims.com/rd.html"
            target="_blank"
            rel="noreferrer"
          >
            Gray–Scott モデルの参考資料
          </a>
        </p>
        <p className={s.prose}>
          <a
            className={s.link}
            href="http://www.scholarpedia.org/article/FitzHugh-Nagumo_model"
            target="_blank"
            rel="noreferrer"
          >
            FitzHugh–Nagumo モデルの参考資料
          </a>
        </p>
      </section>
      <section className={s.section} aria-labelledby="technical-title">
        <h2 className={s.heading} id="technical-title">
          技術的な解説
        </h2>
        <p className={s.prose}>
          Gray–Scott モデルでは、格子の各セルにある2種類の濃度 A と B
          を繰り返し更新します。A を材料に B が増え、外から A が供給される一方で
          B は除去されます。両者は周囲のセルへ拡散し、Feed と Kill
          の組み合わせで斑点や縞などの模様が変わります。
        </p>
        <p className={s.prose}>
          FitzHugh–Nagumo モデルでは、自分を増やす u と、u に増やされて u
          を抑える v の2つの値を更新します。v が u
          より十分速く拡散すると、画面全体に静止した縞や斑点が浮かび上がります。これがチューリングパターンです。v
          がゆっくり戻る設定では u
          の波が伝わり、回り続けるらせんになります。表示するのは u で、0 から 1
          までを色に変換します。
        </p>
        <p className={s.prose}>
          Pebble では、メモリと計算時間を抑えるため、共通の C
          コアで濃度を固定小数点として扱います。現在の時計ビルドは 120 × 136
          セルの Q15 方式を使い、200 × 228
          画素の画面に補間して表示します。ブラウザでは同じ C コアを WebAssembly
          で動かし、同じ解像度の Float32 参照実装と比較できます。
        </p>
        <p className={s.prose}>
          Gray–Scott では描画時に B の濃度を色に変換します。「Pebble の 64
          色に量子化」をオンにすると、各色を2ビットに丸めた時計の表示色を確認できます。「数字を避ける」では文字の下の
          B を 0 に保ち、周囲の除去率も上げて模様を薄くします。FitzHugh–Nagumo
          では文字の下の u を安静値に保ち、周囲の u を安静値へ引き戻します。
        </p>
      </section>
    </>
  );
}
