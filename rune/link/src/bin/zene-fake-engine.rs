//! Test fixture: a fake Zene engine process (`zene_link::fake`). Never started by the UI.
fn main() {
    std::process::exit(zene_link::fake::main_with(std::env::args().collect()));
}
