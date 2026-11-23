//! Minimal reader for Allwinner FEX files (sys_config.fex), following the
//! value rules of the vendor `script` compiler for the types used here.

/// Value of a GPIO description `port:P<letter><pin><mux><pull><drive><data>`.
/// Missing attributes and `default`/`none`/`null` are stored as -1.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Gpio {
    /// Port number: A = 1, B = 2, …
    pub port: i32,
    pub pin: i32,
    pub mux: i32,
    pub pull: i32,
    pub drive: i32,
    pub data: i32,
}

pub struct Fex {
    sections: Vec<(String, Vec<(String, String)>)>,
}

impl Fex {
    /// Parse FEX text. `;` starts a comment, CR characters are ignored, and a
    /// repeated section name continues the earlier section.
    pub fn parse(text: &str) -> Fex {
        let mut fex = Fex {
            sections: Vec::new(),
        };
        let mut current: Option<usize> = None;
        for raw in text.lines() {
            let line = raw.split(';').next().unwrap_or("").replace('\r', "");
            let line = line.trim();
            if let Some(name) = line.strip_prefix('[').and_then(|l| l.split(']').next()) {
                if !line.contains(']') {
                    continue;
                }
                current = Some(match fex.sections.iter().position(|(n, _)| n == name) {
                    Some(index) => index,
                    None => {
                        fex.sections.push((name.to_string(), Vec::new()));
                        fex.sections.len() - 1
                    }
                });
                continue;
            }
            let (Some(index), Some((key, value))) = (current, line.split_once('=')) else {
                continue;
            };
            let (key, value) = (key.trim(), value.trim());
            if !key.is_empty() && !key.contains(char::is_whitespace) && !value.is_empty() {
                fex.sections[index]
                    .1
                    .push((key.to_string(), value.to_string()));
            }
        }
        fex
    }

    /// All `(key, value)` pairs of a section, in file order.
    pub fn section(&self, name: &str) -> &[(String, String)] {
        self.sections
            .iter()
            .find(|(n, _)| n == name)
            .map(|(_, e)| e.as_slice())
            .unwrap_or(&[])
    }

    /// First value of `key` in `section`, as the vendor lookup returns it.
    pub fn get(&self, section: &str, key: &str) -> Option<&str> {
        self.section(section)
            .iter()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v.as_str())
    }

    pub fn int(&self, section: &str, key: &str) -> Result<Option<i32>, String> {
        self.get(section, key)
            .map(|v| parse_int(v).map_err(|e| format!("[{section}] {key}: {e}")))
            .transpose()
    }

    pub fn gpio(&self, section: &str, key: &str) -> Result<Option<Gpio>, String> {
        self.get(section, key)
            .map(|v| parse_gpio(v).map_err(|e| format!("[{section}] {key}: {e}")))
            .transpose()
    }

    /// The GPIO values of a section, in file order.
    pub fn gpios(&self, section: &str) -> Result<Vec<Gpio>, String> {
        self.section(section)
            .iter()
            .filter(|(_, v)| is_gpio(v))
            .map(|(k, v)| parse_gpio(v).map_err(|e| format!("[{section}] {k}: {e}")))
            .collect()
    }
}

pub fn is_gpio(value: &str) -> bool {
    value.starts_with("port:P") || value.starts_with("port:p")
}

/// Integer value: decimal (optionally negative) or `0x` hexadecimal. Values
/// wrap to 32 bits, as the vendor compiler stores them.
pub fn parse_int(value: &str) -> Result<i32, String> {
    let wrapped = if let Some(hex) = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
    {
        u64::from_str_radix(hex, 16).ok().map(|v| v as u32 as i32)
    } else if value
        .bytes()
        .next()
        .is_some_and(|b| b.is_ascii_digit() || b == b'-')
    {
        value.parse::<i64>().ok().map(|v| v as i32)
    } else {
        None
    };
    wrapped.ok_or_else(|| format!("unsupported integer value '{value}'"))
}

fn parse_attribute(attr: &str) -> Result<i32, String> {
    match attr.to_ascii_lowercase().as_str() {
        "default" | "none" | "null" | "-1" => Ok(-1),
        a => a
            .parse::<i32>()
            .map_err(|_| format!("unsupported GPIO attribute '{attr}'")),
    }
}

pub fn parse_gpio(value: &str) -> Result<Gpio, String> {
    let bad = || format!("unsupported GPIO value '{value}'");
    let rest = value.strip_prefix("port:").ok_or_else(bad)?;
    let mut chars = rest.chars();
    if !matches!(chars.next(), Some('P' | 'p')) {
        return Err(bad());
    }
    let letter = chars
        .next()
        .filter(char::is_ascii_alphabetic)
        .ok_or_else(bad)?;
    let rest = chars.as_str();
    let pin_len = rest.find('<').unwrap_or(rest.len());
    let pin = rest[..pin_len].parse::<i32>().map_err(|_| bad())?;
    let mut attrs = Vec::new();
    let mut tail = &rest[pin_len..];
    while !tail.is_empty() {
        let inner = tail.strip_prefix('<').ok_or_else(bad)?;
        let close = inner.find('>').ok_or_else(bad)?;
        attrs.push(parse_attribute(&inner[..close])?);
        tail = &inner[close + 1..];
    }
    if attrs.len() > 4 {
        return Err(bad());
    }
    attrs.resize(4, -1);
    Ok(Gpio {
        port: letter.to_ascii_uppercase() as i32 - 'A' as i32 + 1,
        pin,
        mux: attrs[0],
        pull: attrs[1],
        drive: attrs[2],
        data: attrs[3],
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn integers() {
        assert_eq!(parse_int("528"), Ok(528));
        assert_eq!(parse_int("-1267173226"), Ok(-1_267_173_226));
        assert_eq!(parse_int("0x210"), Ok(0x210));
        assert_eq!(parse_int("0xffffffff"), Ok(-1));
        assert!(parse_int("\"text\"").is_err());
        assert!(parse_int("12a").is_err());
    }

    #[test]
    fn gpio_values() {
        let g = parse_gpio("port:PE2<6><1><default><default>").unwrap();
        assert_eq!(
            g,
            Gpio {
                port: 5,
                pin: 2,
                mux: 6,
                pull: 1,
                drive: -1,
                data: -1
            }
        );
        let g = parse_gpio("port:PF25<2><1><3><default>").unwrap();
        assert_eq!((g.port, g.pin, g.drive), (6, 25, 3));
        let g = parse_gpio("port:PE4<7><none><null>").unwrap();
        assert_eq!((g.mux, g.pull, g.drive, g.data), (7, -1, -1, -1));
        assert!(parse_gpio("port:power0<1>").is_err());
        assert!(parse_gpio("port:PE2<1><2><3><4><5>").is_err());
    }

    #[test]
    fn sections_and_lookup() {
        let fex = Fex::parse(
            "[a]\r\nx = 1 ; comment\r\ny=port:PA1<2>\r\n[b]\r\nx = 2\r\n[a]\r\nz = 3\r\nx = 9\r\n",
        );
        assert_eq!(fex.get("a", "x"), Some("1"));
        assert_eq!(fex.int("a", "z"), Ok(Some(3)));
        assert_eq!(fex.int("b", "x"), Ok(Some(2)));
        assert_eq!(fex.int("a", "missing"), Ok(None));
        assert_eq!(fex.gpios("a").unwrap().len(), 1);
        assert_eq!(fex.section("a").len(), 4);
    }
}
