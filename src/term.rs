use egui::{Color32, FontId, Pos2, Rect, Sense, Stroke, Ui, Vec2};
use vt100::Parser;

pub struct TerminalState {
    pub parser: Parser,
    pub font_size: f32,
    pub cols: u16,
    pub rows: u16,
}

impl TerminalState {
    pub fn new(rows: u16, cols: u16, font_size: f32) -> Self {
        Self {
            parser: Parser::new(rows, cols, 10_000),
            font_size,
            cols,
            rows,
        }
    }

    pub fn feed(&mut self, bytes: &[u8]) {
        self.parser.process(bytes);
    }

    pub fn clear(&mut self) {
        self.parser = Parser::new(self.rows, self.cols, 10_000);
    }
}

pub struct PendingResize {
    pub cols: u16,
    pub rows: u16,
}

pub fn show(
    ui: &mut Ui,
    st: &mut TerminalState,
    mut on_input: impl FnMut(Vec<u8>),
) -> Option<PendingResize> {
    let font_id = FontId::monospace(st.font_size);
    let (cell_w, cell_h) = ui.fonts(|f| (f.glyph_width(&font_id, 'M'), f.row_height(&font_id)));

    let avail = ui.available_size();
    let (rect, response) = ui.allocate_exact_size(avail, Sense::click_and_drag());

    let new_cols = ((rect.width() / cell_w).max(1.0) as u16).max(1);
    let new_rows = ((rect.height() / cell_h).max(1.0) as u16).max(1);

    let mut resize = None;
    if new_cols != st.cols || new_rows != st.rows {
        st.parser.set_size(new_rows, new_cols);
        st.cols = new_cols;
        st.rows = new_rows;
        resize = Some(PendingResize {
            cols: new_cols,
            rows: new_rows,
        });
    }

    let default_bg = Color32::from_rgb(15, 18, 26);
    let default_fg = Color32::from_rgb(204, 215, 235);

    let painter = ui.painter_at(rect);
    painter.rect_filled(rect, 0.0, default_bg);

    let screen = st.parser.screen();
    for row in 0..st.rows {
        for col in 0..st.cols {
            if let Some(cell) = screen.cell(row, col) {
                let x = rect.min.x + col as f32 * cell_w;
                let y = rect.min.y + row as f32 * cell_h;
                let cell_rect = Rect::from_min_size(Pos2::new(x, y), Vec2::new(cell_w, cell_h));
                let bg = color_to_egui(cell.bgcolor(), false, default_bg, default_fg);
                if bg != default_bg {
                    painter.rect_filled(cell_rect, 0.0, bg);
                }
                let contents = cell.contents();
                if !contents.is_empty() && contents != " " {
                    let fg = color_to_egui(cell.fgcolor(), true, default_bg, default_fg);
                    painter.text(
                        Pos2::new(x, y),
                        egui::Align2::LEFT_TOP,
                        contents,
                        font_id.clone(),
                        fg,
                    );
                }
            }
        }
    }

    if !screen.hide_cursor() {
        let (cy, cx) = screen.cursor_position();
        if cx < st.cols && cy < st.rows {
            let px = rect.min.x + cx as f32 * cell_w;
            let py = rect.min.y + cy as f32 * cell_h;
            let cur_rect = Rect::from_min_size(Pos2::new(px, py), Vec2::new(cell_w, cell_h));
            painter.rect_stroke(
                cur_rect,
                0.0,
                Stroke::new(1.5, Color32::from_rgb(150, 200, 255)),
            );
        }
    }

    if response.clicked() {
        response.request_focus();
    }
    if response.has_focus() {
        let events = ui.input(|i| i.events.clone());
        for ev in events {
            match ev {
                egui::Event::Text(s) => on_input(s.into_bytes()),
                egui::Event::Key {
                    key,
                    pressed: true,
                    modifiers,
                    ..
                } => {
                    if let Some(bytes) = crate::keys::key_to_bytes(key, modifiers) {
                        on_input(bytes);
                    }
                }
                _ => {}
            }
        }
    }

    resize
}

fn color_to_egui(c: vt100::Color, is_fg: bool, def_bg: Color32, def_fg: Color32) -> Color32 {
    match c {
        vt100::Color::Default => {
            if is_fg {
                def_fg
            } else {
                def_bg
            }
        }
        vt100::Color::Idx(i) => palette(i),
        vt100::Color::Rgb(r, g, b) => Color32::from_rgb(r, g, b),
    }
}

const PALETTE_16: [(u8, u8, u8); 16] = [
    (15, 18, 26),
    (255, 145, 165),
    (92, 230, 175),
    (255, 213, 140),
    (130, 178, 255),
    (196, 167, 255),
    (106, 217, 226),
    (204, 215, 235),
    (130, 140, 160),
    (255, 165, 185),
    (130, 240, 195),
    (255, 225, 160),
    (165, 195, 255),
    (214, 191, 255),
    (150, 232, 240),
    (224, 235, 255),
];

fn palette(idx: u8) -> Color32 {
    if (idx as usize) < PALETTE_16.len() {
        let (r, g, b) = PALETTE_16[idx as usize];
        Color32::from_rgb(r, g, b)
    } else if (16..=231).contains(&idx) {
        let n = idx - 16;
        let r = n / 36;
        let g = (n / 6) % 6;
        let b = n % 6;
        let scale = |c: u8| if c == 0 { 0 } else { 55 + c * 40 };
        Color32::from_rgb(scale(r), scale(g), scale(b))
    } else {
        let v = 8u16.saturating_add((idx as u16 - 232) * 10).min(255) as u8;
        Color32::from_rgb(v, v, v)
    }
}
