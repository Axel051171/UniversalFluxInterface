# Contributing to UFI

Thanks for your interest in UFI!

## How to Contribute

### Bug Reports

1. Check if the issue already exists
2. Include: hardware version, firmware version, steps to reproduce
3. Add screenshots or logs if applicable

### Feature Requests

Open an issue describing:
- The use case
- Why it's useful for preservation

### Code Contributions

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Test thoroughly
5. Commit with clear message: `git commit -m "Add feature X"`
6. Push: `git push origin feature/my-feature`
7. Open a Pull Request

## Code Style

### C (Firmware)

- 4 spaces indentation (no tabs)
- `snake_case` for functions and variables
- `UPPER_CASE` for constants and macros
- Module prefix: `ufi_module_function()`

Example:
```c
void ufi_flux_capture_start(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return;
    }
    // ...
}
```

### KiCad

- Grid: 0.1mm for symbols, 0.25mm for footprints
- Label all pins clearly
- Use reference designators consistently

## Testing

- Test on real hardware when possible
- Run DRC checks in KiCad before submitting PCB changes
- Verify firmware compiles without warnings

## Questions?

Open an issue or start a discussion.
