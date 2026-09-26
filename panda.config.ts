import {
  defineConfig,
  defineTextStyles,
  defineGlobalStyles,
} from "@pandacss/dev";

// Theme Tokens from:
// * pixiv Charcoal Design Token & Foundation
//   licensed under CC BY 4.0
//   https://www.figma.com/community/file/1415744382766483894/charcoal-design-token-foundation
// and
// * pixiv Charcoal Primitive Tokens
//   licensed under CC BY 4.0
//   https://www.figma.com/community/file/1415733518936785321
//
const textStyles = defineTextStyles({
  "heading/xxxl": {
    description: "Heading XXXL",
    value: {
      fontWeight: "bold",
      fontSize: "40px",
      lineHeight: "52px",
    },
  },
  "heading/xxl": {
    description: "Heading XXL",
    value: {
      fontWeight: "bold",
      fontSize: "36px",
      lineHeight: "44px",
    },
  },
  "heading/xl": {
    description: "Heading XL",
    value: {
      fontWeight: "bold",
      fontSize: "32px",
      lineHeight: "40px",
    },
  },
  "heading/l": {
    description: "Heading L",
    value: {
      fontWeight: "bold",
      fontSize: "28px",
      lineHeight: "36px",
    },
  },
  "heading/m": {
    description: "Heading M",
    value: {
      fontWeight: "bold",
      fontSize: "25px",
      lineHeight: "32px",
    },
  },
  "heading/s": {
    description: "Heading S",
    value: {
      fontWeight: "bold",
      fontSize: "22px",
      lineHeight: "28px",
    },
  },
  "heading/xs": {
    description: "Heading XS",
    value: {
      fontWeight: "bold",
      fontSize: "20px",
      lineHeight: "28px",
    },
  },
  "heading/xxs": {
    description: "Heading XXS",
    value: {
      fontWeight: "bold",
      fontSize: "18px",
      lineHeight: "24px",
    },
  },
  "heading/xxxs": {
    description: "Heading XXXS",
    value: {
      fontWeight: "bold",
      fontSize: "14px",
      lineHeight: "20px",
    },
  },
  "content/heading/xxl/noruby": {
    description: "Heading XXL",
    value: {
      fontWeight: "bold",
      fontSize: "36px",
      lineHeight: "48px",
    },
  },
  "content/heading/m": {
    value: {
      fontWeight: "bold",
      fontSize: "1.6rem",
      lineHeight: "2.8rem",
    },
  },
  "content/heading/s": {
    value: {
      fontWeight: "bold",
      fontSize: "1.4rem",
      lineHeight: "2.6rem",
    },
  },
  "content/heading/xs": {
    value: {
      fontWeight: "bold",
      fontSize: "1.2rem",
      lineHeight: "2.2rem",
    },
  },
  "content/heading/xxs": {
    value: {
      fontWeight: "bold",
      fontSize: "1.2rem",
      lineHeight: "2rem",
    },
  },
  "content/heading/xxxs": {
    description: "Heading XXXS",
    value: {
      fontWeight: "bold",
      fontSize: "0.8rem",
      lineHeight: "1.8rem",
    },
  },
  "content/paragraph": {
    description: "paragraph with normal size",
    value: {
      fontWeight: "normal",
      fontSize: "1rem",
      lineHeight: "2.5rem",
    },
  },
  "content/paragraph/code": {
    description: "code paragraph with normal size",
    value: {
      fontWeight: "normal",
      fontSize: "1rem",
      lineHeight: "1.5rem",
      fontFamily: "var(--font-roboto-mono)",
    },
  },
  "paragraph/code": {
    description: "code paragraph",
    value: {
      fontWeight: "normal",
      fontSize: "16px",
      lineHeight: "22px",
      fontFamily: "var(--font-roboto-mono)",
    },
  },
  "caption/bold/m": {
    description: "medium bold caption",
    value: {
      fontWeight: "bold",
      fontSize: "14px",
      lineHeight: "20px",
    },
  },
  "caption/regular/m": {
    description: "medium caption",
    value: {
      fontWeight: "normal",
      fontSize: "14px",
      lineHeight: "20px",
    },
  },
  "caption/bold/s": {
    description: "small bold caption",
    value: {
      fontWeight: "bold",
      fontSize: "12px",
      lineHeight: "18px",
    },
  },
  "caption/regular/s": {
    description: "small caption",
    value: {
      fontWeight: "normal",
      fontSize: "12px",
      lineHeight: "18px",
    },
  },
});

const keyframes = {
  "progress-transform": {
    from: {
      transform: "scaleY(0)",
    },
    to: {
      transform: "scaleY(1)",
    },
  },
  "fade-in": {
    from: {
      filter: "blur(10px)",
      opacity: "0",
    },
    to: {
      opacity: "1",
    },
  },
  "fade-out": {
    from: {
      opacity: "1",
    },
    to: {
      opacity: "0",
    },
  },
  "show-label": {
    "0%, 45%": {
      opacity: "0",
    },
    "48%, 52%": {
      opacity: "1",
    },
    "55%, 100%": {
      opacity: "0",
    },
  },
  spin: {
    "0%": {
      transform: "rotate(0deg)",
    },
    "100%": {
      transform: "rotate(360deg)",
    },
  },
  // 拍手中のボタンの色相を一周させる。明度・彩度は固定なのでトーンは変わらない
  "clap-hue": {
    from: {
      "--clap-hue": "0deg",
    },
    to: {
      "--clap-hue": "360deg",
    },
  },
};

const colorValue = (light: string, dark: string) => {
  return {
    value: {
      base: light,
      _light: light,
      _dark: dark,
      _osDark: dark,
    },
  };
};

const theme = {
  breakpoints: {
    md: "744px",
    lg: "952px",
    xl: "1160px",
    "2xl": "1368px",
  },
  tokens: {
    colors: {
      neutral: {
        light: {
          0: { value: "#ffffff" },
          5: { value: "#f3f3f3" },
          10: { value: "#e8e8e8" },
          20: { value: "#d9d9d9" },
          30: { value: "#c2c2c2" },
          40: { value: "#acacac" },
          50: { value: "#949494" },
          60: { value: "#717171" },
          70: { value: "#515151" },
          80: { value: "#383838" },
          90: { value: "#1f1f1f" },
        },
        dark: {
          "-10": { value: "#060606" },
          "-5": { value: "#151515" },
          0: { value: "#1F1F1F" },
          5: { value: "#292929" },
          10: { value: "#333333" },
          20: { value: "#515151" },
          30: { value: "#707070" },
          40: { value: "#828282" },
          50: { value: "#979797" },
          60: { value: "#AFAFAF" },
          70: { value: "#BCBCBC" },
          80: { value: "#CACACA" },
          90: { value: "#E4E4E4" },
        },
      },
      "neutral-a": {
        light: {
          0: { value: "rgba(31, 31, 31, 0)" },
          5: { value: "rgba(31, 31, 31, 0.05)" },
          10: { value: "rgba(31, 31, 31, 0.1)" },
          20: { value: "rgba(31, 31, 31, 0.17)" },
          30: { value: "rgba(31, 31, 31, 0.27)" },
          40: { value: "rgba(31, 31, 31, 0.37)" },
          50: { value: "rgba(31, 31, 31, 0.47)" },
          60: { value: "rgba(31, 31, 31, 0.63)" },
          70: { value: "rgba(31, 31, 31, 0.77)" },
          80: { value: "rgba(31, 31, 31, 0.88)" },
          90: { value: "rgba(31, 31, 31, 1)" },
        },
        dark: {
          "-10": { value: "rgba(6, 6, 6, 1)" },
          "-5": { value: "rgba(6, 6, 6, 0.41)" },
          0: { value: "rgba(228, 228, 228, 0)" },
          5: { value: "rgba(228, 228, 228, 0.05)" },
          10: { value: "rgba(228, 228, 228, 0.1)" },
          20: { value: "rgba(228, 228, 228, 0.25)" },
          30: { value: "rgba(228, 228, 228, 0.41)" },
          40: { value: "rgba(228, 228, 228, 0.5)" },
          50: { value: "rgba(228, 228, 228, 0.61)" },
          60: { value: "rgba(228, 228, 228, 0.73)" },
          70: { value: "rgba(228, 228, 228, 0.8)" },
          80: { value: "rgba(228, 228, 228, 0.87)" },
          90: { value: "rgba(228, 228, 228, 1)" },
        },
      },
      blue: {
        light: {
          5: { value: "#ECF4FD" },
          10: { value: "#D8EBFB" },
          20: { value: "#BCDEFC" },
          30: { value: "#89C8FD" },
          40: { value: "#55B2FD" },
          50: { value: "#0096FA" },
          60: { value: "#1F75BC" },
          70: { value: "#185182" },
          80: { value: "#133A5D" },
          90: { value: "#03233F" },
        },
        dark: {
          "-10": { value: "#060606" },
          "-5": { value: "#151515" },
          0: { value: "#1F1F1F" },
          5: { value: "#212932" },
          10: { value: "#243749" },
          20: { value: "#27547E" },
          30: { value: "#0872BE" },
          40: { value: "#3788D0" },
          50: { value: "#539CE0" },
          60: { value: "#72B5F5" },
          70: { value: "#8BC1F8" },
          80: { value: "#A6CDF5" },
          90: { value: "#CFE6FD" },
        },
      },
      purple: {
        light: {
          5: { value: "#F4F1FC" },
          10: { value: "#ECE5FB" },
          20: { value: "#E0D2FD" },
          30: { value: "#CFB7FD" },
          40: { value: "#BE99FD" },
          50: { value: "#AD78FC" },
          60: { value: "#8F4DE1" },
          70: { value: "#6727AB" },
          80: { value: "#462073" },
          90: { value: "#281046" },
        },
        dark: {
          "-10": { value: "#060606" },
          "-5": { value: "#151515" },
          0: { value: "#1F1F1F" },
          5: { value: "#2A2631" },
          10: { value: "#383047" },
          20: { value: "#5D4484" },
          30: { value: "#8358C2" },
          40: { value: "#956ED2" },
          50: { value: "#A985E5" },
          60: { value: "#BFA0F6" },
          70: { value: "#C9B0F9" },
          80: { value: "#D2C0F5" },
          90: { value: "#E9DFFF" },
        },
      },
    },
    spacing: {
      "0": { value: "0px" },
      10: { value: "4px" },
      20: { value: "8px" },
      25: { value: "12px" },
      30: { value: "16px" },
      40: { value: "24px" },
      50: { value: "40px" },
      60: { value: "64px" },
      70: { value: "104px" },
      80: { value: "168px" },
      90: { value: "272px" },
      100: { value: "440px" },
    },
    radii: {
      xs: { value: "2px" },
      s: { value: "4px" },
      m: { value: "8px" },
      xl: { value: "16px" },
      xxl: { value: "24px" },
      full: { value: "9999px" },
    },
    sizes: {
      1: { value: `${80 * 1 + 0 * 24}px` },
      2: { value: `${80 * 2 + 1 * 24}px` },
      3: { value: `${80 * 3 + 2 * 24}px` },
      4: { value: `${80 * 4 + 3 * 24}px` },
      5: { value: `${80 * 5 + 4 * 24}px` },
      6: { value: `${80 * 6 + 5 * 24}px` },
      7: { value: `${80 * 7 + 6 * 24}px` },
      8: { value: `${80 * 8 + 7 * 24}px` },
      9: { value: `${80 * 9 + 8 * 24}px` },
      10: { value: `${80 * 10 + 9 * 24}px` },
      11: { value: `${80 * 11 + 10 * 24}px` },
      12: { value: `${80 * 12 + 11 * 24}px` },
      full: { value: "100%" },
    },
    fontSizes: {
      xxxl: { value: "40px" }, // line-height: 52px
      xxl: { value: "36px" }, // line-height: 44px
      xl: { value: "32px" }, // line-height: 40px
      l: { value: "28px" }, // line-height: 36px
      m: { value: "25px" }, // line-height: 32px
      s: { value: "22px" }, // line-height: 28px
      xs: { value: "20px" }, // line-height: 28px
      xxs: { value: "18px" }, // line-height: 20px
      xxxs: { value: "14px" }, // line-height: 18px
      body: { value: "16px" }, // line-height: 20px
    },
  },
  semanticTokens: {
    colors: {
      background: {
        DEFAULT: colorValue(
          "{colors.neutral.light.0}",
          "{colors.neutral.dark.0}"
        ),
        secondary: colorValue(
          "{colors.neutral.light.5}",
          "{colors.neutral.dark.-5}"
        ),
        tertiary: colorValue(
          "{colors.neutral.light.10}",
          "{colors.neutral.dark.-10}"
        ),
        invert: colorValue(
          "{colors.neutral.dark.0}",
          "{colors.neutral.light.0}"
        ),
        "invert-hover": colorValue(
          "{colors.neutral.dark.5}",
          "{colors.neutral.light.5}"
        ),
      },
      container: {
        DEFAULT: colorValue(
          "{colors.neutral.light.0}",
          "{colors.neutral.dark.0}"
        ),
        hover: colorValue(
          "{colors.neutral.light.5}",
          "{colors.neutral.dark.5}"
        ),
        press: colorValue(
          "{colors.neutral.light.10}",
          "{colors.neutral.dark.10}"
        ),
        "default-a": colorValue(
          "{colors.neutral-a.light.0}",
          "{colors.neutral-a.dark.0}"
        ),
        "hover-a": colorValue(
          "{colors.neutral-a.light.5}",
          "{colors.neutral-a.dark.5}"
        ),
        "press-a": colorValue(
          "{colors.neutral-a.light.10}",
          "{colors.neutral-a.dark.10}"
        ),
        secondary: {
          DEFAULT: colorValue(
            "{colors.neutral-a.light.5}",
            "{colors.neutral-a.dark.5}"
          ),
          hover: colorValue(
            "{colors.neutral-a.light.10}",
            "{colors.neutral-a.dark.10}"
          ),
          press: colorValue(
            "{colors.neutral-a.light.20}",
            "{colors.neutral-a.dark.20}"
          ),
          "default-a": colorValue(
            "{colors.neutral-a.light.5}",
            "{colors.neutral-a.dark.5}"
          ),
          "hover-a": colorValue(
            "{colors.neutral-a.light.10}",
            "{colors.neutral-a.dark.10}"
          ),
          "press-a": colorValue(
            "{colors.neutral-a.light.20}",
            "{colors.neutral-a.dark.20}"
          ),
        },
        primary: {
          DEFAULT: colorValue(
            "{colors.blue.light.50}",
            "{colors.blue.dark.30}"
          ),
          hover: colorValue("{colors.blue.light.60}", "{colors.blue.dark.40}"),
          press: colorValue("{colors.blue.light.70}", "{colors.blue.dark.50}"),
        },
        hud: colorValue(
          "{colors.neutral.light.80}",
          "{colors.neutral.dark.90}"
        ),
      },
      text: {
        DEFAULT: colorValue(
          "{colors.neutral.light.90}",
          "{colors.neutral.dark.90}"
        ),
        hover: colorValue(
          "{colors.neutral.light.80}",
          "{colors.neutral.dark.80}"
        ),
        press: colorValue(
          "{colors.neutral.light.70}",
          "{colors.neutral.dark.70}"
        ),
        disable: colorValue(
          "{colors.neutral.light.30}",
          "{colors.neutral.dark.40}"
        ),
        secondary: {
          DEFAULT: colorValue(
            "{colors.neutral.light.70}",
            "{colors.neutral.dark.60}"
          ),
          hover: colorValue(
            "{colors.neutral.light.80}",
            "{colors.neutral.dark.70}"
          ),
          press: colorValue(
            "{colors.neutral.light.90}",
            "{colors.neutral.dark.80}"
          ),
        },
        tertiary: {
          DEFAULT: colorValue(
            "{colors.neutral.light.60}",
            "{colors.neutral.dark.40}"
          ),
          hover: colorValue(
            "{colors.neutral.light.70}",
            "{colors.neutral.dark.60}"
          ),
          press: colorValue(
            "{colors.neutral.light.80}",
            "{colors.neutral.dark.70}"
          ),
        },
        info: {
          DEFAULT: colorValue(
            "{colors.blue.light.60}",
            "{colors.blue.dark.60}"
          ),
          hover: colorValue("{colors.blue.light.70}", "{colors.blue.dark.80}"),
          press: colorValue("{colors.blue.light.80}", "{colors.blue.dark.90}"),
          secondary: {
            DEFAULT: colorValue(
              "{colors.blue.light.70}",
              "{colors.blue.dark.70}"
            ),
            hover: colorValue(
              "{colors.blue.light.80}",
              "{colors.blue.dark.80}"
            ),
            press: colorValue(
              "{colors.blue.light.90}",
              "{colors.blue.dark.90}"
            ),
          },
          tertiary: {
            DEFAULT: colorValue(
              "{colors.blue.light.50}",
              "{colors.blue.dark.50}"
            ),
            hover: colorValue(
              "{colors.blue.light.60}",
              "{colors.blue.dark.60}"
            ),
            press: colorValue(
              "{colors.blue.light.70}",
              "{colors.blue.dark.70}"
            ),
          },
        },
        visited: {
          DEFAULT: colorValue(
            "{colors.purple.light.70}",
            "{colors.purple.dark.70}"
          ),
          hover: colorValue(
            "{colors.purple.light.80}",
            "{colors.purple.dark.80}"
          ),
          press: colorValue(
            "{colors.purple.light.90}",
            "{colors.purple.dark.90}"
          ),
        },
        onPrimary: {
          DEFAULT: colorValue(
            "{colors.neutral.light.0}",
            "{colors.neutral.dark.90}"
          ),
          hover: colorValue(
            "{colors.neutral.light.0}",
            "{colors.neutral.dark.90}"
          ),
          press: colorValue(
            "{colors.neutral.light.0}",
            "{colors.neutral.dark.90}"
          ),
        },
        onHud: colorValue(
          "{colors.neutral.dark.90}",
          "{colors.neutral.light.90}"
        ),
        invert: colorValue(
          "{colors.neutral.dark.90}",
          "{colors.neutral.light.90}"
        ),
      },
      border: {
        DEFAULT: colorValue(
          "{colors.neutral-a.light.50}",
          "{colors.neutral-a.dark.30}"
        ),
        secondary: colorValue(
          "{colors.neutral-a.light.10}",
          "{colors.neutral-a.dark.10}"
        ),
        focus: {
          1: colorValue("{colors.blue.light.60}", "{colors.blue.dark.60}"),
          2: colorValue("{colors.blue.light.20}", "{colors.blue.dark.20}"),
        },
        transparent: colorValue(
          "{colors.neutral-a.light.0}",
          "{colors.neutral-a.dark.0}"
        ),
      },
      transparent: {
        DEFAULT: colorValue(
          "{colors.neutral-a.light.0}",
          "{colors.neutral-a.dark.0}"
        ),
      },
      scrollbar: {
        thumb: colorValue(
          "{colors.neutral-a.light.50}",
          "{colors.neutral-a.dark.50}"
        ),
        bg: colorValue(
          "{colors.neutral-a.light.0}",
          "{colors.neutral-a.dark.0}"
        ),
      },
      progress: {
        bg: colorValue("{colors.blue.light.10}", "{colors.blue.dark.10}"),
      },
    },
  },
  extend: {
    textStyles,
    keyframes,
  },
};

const globalCss = defineGlobalStyles({
  html: {
    fontFamily: "var(--font-sans-serif)",
    "scrollbar-color":
      "var(--colors-scrollbar-thumb) var(--colors-scrollbar-bg)",
    "scrollbar-gutter": "stable",
  },
  body: {
    m: "0",
    p: "0",
  },
  "h1, h2, h3, h4, h5, h6": {
    m: "0",
    p: "0",
  },
  ".content": {
    "& > *:first-child": {
      marginBlockStart: "0",
    },
    "& h1": {
      marginBlockStart: "40",
      textStyle: "content/heading/xxl/noruby",
    },
    "& h2": {
      marginBlockStart: "40",
      textStyle: "content/heading/m",
    },
    "& h3": {
      marginBlockStart: "40",
      textStyle: "content/heading/s",
    },
    "& h4": {
      marginBlockStart: "40",
      textStyle: "content/heading/xs",
    },
    "& h5": {
      textStyle: "content/heading/xxs",
    },
    "& h6": {
      textStyle: "content/heading/xxxs",
    },
    "& blockquote": {
      m: "0",
      marginInlineEnd: "30",
      paddingInlineStart: "20",
      textStyle: "content/paragraph",
      borderLeftColor: "border.secondary",
      borderLeftWidth: "4px",
      borderLeftStyle: "solid",
    },
    "& pre": {
      marginBlockEnd: "30",
      background: "background.secondary",
      p: "30",
      borderRadius: "m",
      textStyle: "content/paragraph",
      overflow: "auto",
    },
    "& code": {
      textStyle: "content/paragraph/code",
    },
    "& .github-code": {
      marginBlockEnd: "30",
      borderColor: "border.secondary",
      borderWidth: "1px",
      borderStyle: "solid",
      borderRadius: "m",
      overflow: "hidden",
      "& .github-code-meta": {
        display: "flex",
        alignItems: "center",
        justifyContent: "space-between",
        gap: "20",
        m: "0",
        paddingInline: "30",
        paddingBlock: "10",
        background: "background.secondary",
        borderBottomColor: "border.secondary",
        borderBottomWidth: "1px",
        borderBottomStyle: "solid",
        "& a": {
          color: "text.secondary",
          textDecoration: "none",
          "&:hover": {
            color: "text.secondary.hover",
            textDecoration: "underline",
          },
          "&:visited": {
            color: "text.secondary",
          },
        },
        "& code": {
          fontSize: "[0.875rem]",
          lineHeight: "[20px]",
        },
      },
      "& pre": {
        m: "0",
        background: "[transparent]",
        borderRadius: "[0]",
        fontSize: "[0.875rem]",
        lineHeight: "[1.6]",
        "& code": {
          fontSize: "[0.875rem]",
          lineHeight: "[1.6]",
        },
      },
      "& .github-code-copy": {
        flexShrink: "0",
        display: "inline-flex",
        alignItems: "center",
        justifyContent: "center",
        width: "[28px]",
        height: "[28px]",
        p: "0",
        background: "[transparent]",
        border: "none",
        borderRadius: "m",
        color: "text.secondary",
        cursor: "pointer",
        "&:hover": {
          background: "container.hover",
          color: "text.secondary.hover",
        },
        "&.copied": {
          color: "[#1a7f37]",
        },
      },
    },
    "html.dark & .github-code .github-code-copy.copied": {
      color: "[#3fb950]",
    },
    "& ul": {
      m: "0",
      paddingInlineStart: "40",
      listStyleType: "disc",
      textStyle: "content/paragraph",
    },
    "& ol": {
      m: "0",
      paddingInlineStart: "40",
      listStyleType: "decimal",
      textStyle: "content/paragraph",
    },
    "& > ul, & > ol": {
      marginBlockStart: "30",
      marginBlockEnd: "30",
    },
    "& li": {
      marginBlockStart: "10",
      marginBlockEnd: "10",
    },
    "& p": {
      marginBlockEnd: "40",
      textStyle: "content/paragraph",
    },
    "& a": {
      color: "text.info",
      textDecoration: "underline",
      "&:hover": {
        color: "text.info.hover",
      },
      "&:active": {
        color: "text.info.press",
      },
      "&:visited": {
        color: "text.visited",
        "&:hover": {
          color: "text.visited.hover",
        },
        "&:active": {
          color: "text.visited.press",
        },
      },
    },
    "& hr": {
      borderColor: "border.secondary",
      borderWidth: "1px 0 0 0",
      borderStyle: "solid",
      my: "50",
    },
    "& p img": {
      maxWidth: "100%",
      height: "auto",
      borderRadius: "m",
      my: "40",
    },
    "& .tweet-card a": {
      color: "text.secondary",
      textDecoration: "none",
      "&:hover": {
        color: "text.secondary.hover",
        textDecoration: "none",
      },
      "&:visited": {
        color: "text.secondary",
        "&:hover": {
          color: "text.secondary.hover",
        },
      },
    },
    "& .tweet-card .tweet-name": {
      color: "text",
      "&:hover": {
        color: "text.hover",
        textDecoration: "underline",
      },
    },
    "& .tweet-card .tweet-handle:hover": {
      textDecoration: "underline",
    },
    "& .tweet-card .tweet-body a, & .tweet-card .tweet-quote-body a": {
      color: "text.info",
      textDecoration: "underline",
      "&:hover": {
        color: "text.info.hover",
        textDecoration: "underline",
      },
      "&:visited": {
        color: "text.visited",
        "&:hover": {
          color: "text.visited.hover",
        },
      },
    },
    "& .tweet-card .tweet-action-reply:hover": {
      color: "[#1d9bf0]",
      background: "container.hover",
    },
    "& .tweet-card .tweet-action-like:hover": {
      color: "[#f91880]",
      background: "container.hover",
    },
    "& .toc": {
      display: "none",
      xl: {
        display: "block",
        position: "absolute",
        top: "50",
        height: "100%",
        right: "[-244px]",
      },
      "& .toc-contents": {
        position: "sticky",
        top: "50",
        width: "2",
      },
      "& .toc-title": {
        mt: "20",
        my: "20",
        textStyle: "heading/xxs",
      },
      "& .toc-level": {
        p: "0",
        listStyleType: "none",
      },
      "& .toc-item": {
        m: "0",
      },
      "& .toc-link": {
        textStyle: "caption/regular/s",
        display: "inline-block",
        px: "30",
        py: "10",
        color: "text.secondary",
        textDecoration: "none",
        "&:hover": {
          color: "text.secondary.hover",
          textDecoration: "underline",
        },
        "&:active": {
          color: "text.secondary.press",
        },
        "&:visited": {
          color: "text.secondary",
        },
        "&.active": {
          color: "text.info.tertiary",

          _hover: {
            color: "text.info.tertiary.hover",
          },
          _active: {
            color: "text.info.tertiary.press",
          },
        },
      },
      "& .toc-level-1 > .toc-item > .toc-link": {
        px: "0",
        textStyle: "caption/bold/s",
      },
    },
  },
  "& .content.secondary-content": {
    "& a": {
      color: "text.info.secondary",
      "&:hover": {
        color: "text.info.secondary.hover",
      },
      "&:active": {
        color: "text.info.secondary.press",
      },
    },
  },
  ".header-image": {
    width: "100%",
    height: "auto",
    borderRadius: "m",
    my: "40",
  },
  ".figure-image": {
    my: "40",
    mx: "0",
    "& img": {
      width: "100%",
      height: "auto",
      borderRadius: "m",
    },
    "& figcaption": {
      mt: "10",
      textAlign: "center",
      textStyle: "caption/regular/s",
      lineHeight: "2.5em",
      color: "text.secondary",
    },
  },
  ".ruby-placeholder": {
    position: "relative",
    display: "inline-block",
    "&::after": {
      content: "attr(data-ruby-text)",
      position: "absolute",
      top: "0",
      left: "50%",
      transform: "translate(-50%, 0)",
      display: "block",
      fontSize: "0.55rem",
      lineHeight: "1",
      letterSpacing: "0",
      whiteSpace: "nowrap",
    },
  },
  "a .ruby-placeholder": {
    textDecoration: "underline",
  },
  "h2 .ruby-placeholder::after": {
    transform: "translate(-50%, -0.3rem)",
    fontSize: "0.7rem",
  },
  "h3 .ruby-placeholder::after": {
    transform: "translate(-50%, -0.3rem)",
    fontSize: "0.7rem",
  },
  "h4 .ruby-placeholder::after": {
    transform: "translate(-50%, -0.2rem)",
    fontSize: "0.6rem",
  },
  "h5 .ruby-placeholder::after": {
    transform: "translate(-50%, -0.2rem)",
    fontSize: "0.6rem",
  },
  "figcaption .ruby-placeholder::after": {
    fontSize: "0.5rem",
  },
  "html.ruby-disabled .ruby-placeholder::after": {
    content: "none",
  },

  // 足あと (閲覧数・拍手) の島。他の島と同じく素のクラス名で当てることで、
  // Panda のランタイムをクライアントのバンドルに持ち込まない
  ".page-stat": {
    display: "inline-flex",
    alignItems: "center",
    gap: "10",
  },
  ".page-stat-icon": {
    display: "inline-flex",
    alignItems: "center",
  },
  // Font Awesome の SVG は寸法を持たず、charcoal のアイコンより字面も大きい
  ".page-stat-icon.is-claps svg": {
    width: "[14px]",
    height: "[14px]",
  },
  // 昔の個人サイトのアクセスカウンタ。数字は島がインライン SVG で描く。
  // 桁の枠は 14x18 で、本文の line-height (18px) にそろえてある
  ".visitor-counter": {
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    flexWrap: "wrap",
    gap: "10",
    my: "40",
    // トップページの他のキャプション (「日々の記録」など) と同じ
    textStyle: "caption/bold/s",
  },
  // 数字が届くまでは見せないが、場所は取っておく
  ".visitor-counter.is-pending": {
    visibility: "hidden",
  },
  ".visitor-counter-digits": {
    display: "inline-flex",
    gap: "[1px]",
    p: "[1px]",
    borderWidth: "1px",
    borderStyle: "solid",
    borderColor: "[#000]",
    borderRadius: "[2px]",
    background: "[#000]",
  },
  ".visitor-counter-digit": {
    display: "inline-flex",
    alignItems: "center",
    justifyContent: "center",
    width: "[14px]",
    height: "[18px]",
    background: "[linear-gradient(180deg, #222 0%, #111 100%)]",
  },
  // 4x6 のグリッドを 1ドット 2px で描くので 8x12。枠 14x18 の中に
  // 上下左右 3px の余白ができる
  ".visitor-counter-digit svg": {
    width: "[8px]",
    height: "[12px]",
    fill: "[#f4f4f4]",
  },
  ".clap-button": {
    display: "inline-flex",
    boxSizing: "border-box",
    alignItems: "center",
    justifyContent: "center",
    gap: "20",
    minHeight: "[56px]",
    px: "50",
    // ボーダーがあると背景が届かない1pxのふちができるので持たせない。
    // フォーカスはアウトラインだけで示す
    borderStyle: "none",
    borderRadius: "full",
    backgroundColor: "background.invert",
    color: "text.invert",
    // 文字サイズは本文と同じ
    fontSize: "body",
    fontWeight: "bold",
    lineHeight: "[1.5]",
    cursor: "pointer",
    touchAction: "manipulation",
    transition: "all",
    transitionDuration: "200ms",
    "&:hover": {
      backgroundColor: "background.invert-hover",
    },
    "&:focus-visible": {
      outlineStyle: "[solid]",
      outlineWidth: "[2px]",
      outlineOffset: "[2px]",
      outlineColor: "border.focus.2",
    },
    // 拍手中は単色のまま色相だけを巡らせる。hsl だと同じ明度でも青が暗く
    // 黄が明るく見えてしまうので、知覚的に均等な oklch を使う
    "&[data-clapping='true'], &[data-clapping='true']:hover": {
      backgroundColor: "[oklch(0.7 0.16 var(--clap-hue, 0deg))]",
      color: "[#ffffff]",
      boxShadow: "[0 0 20px oklch(0.7 0.16 var(--clap-hue, 0deg) / 0.5)]",
      animation: "clap-hue 3.5s linear infinite",
    },
    // charcoal は disabled を「色はそのままで opacity 0.32」で表す
    // (@charcoal-ui/react の Button/index.css と同じ扱い)
    "&[aria-disabled='true'], &[aria-disabled='true']:hover": {
      cursor: "default",
      opacity: "[0.32]",
      backgroundColor: "background.invert",
    },
    "@media (prefers-reduced-motion: reduce)": {
      "&[data-clapping='true']": {
        animation: "none",
      },
    },
  },
  ".clap-button-icon": {
    display: "inline-flex",
    alignItems: "center",
    // 押したときは奥の手に向かって閉じるので、縮む基準を右端に置く
    transformOrigin: "[right center]",
    "& svg": {
      width: "[24px]",
      height: "[24px]",
    },
  },
  // 画面には出さず読み上げにだけ渡す。拍手の状態と、カウンタの数字
  // (図形で描いているので本文が要る)
  ".clap-status, .visitor-counter-value": {
    position: "absolute",
    width: "[1px]",
    height: "[1px]",
    p: "0",
    m: "[-1px]",
    overflow: "hidden",
    whiteSpace: "nowrap",
    clipPath: "[inset(50%)]",
  },
});

export default defineConfig({
  outdir: "styled-system",
  include: ["./src/**/*.{ts,tsx}"],
  exclude: [],
  presets: ["@pandacss/preset-base"],
  theme: theme,
  globalCss,
  strictTokens: true,
  strictPropertyValues: true,
});
