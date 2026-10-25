"""Heap bounds, PSRAM failure selection and deliberately broken ELF budgets."""
import sys
from pathlib import Path
from test_regressions import BASE, ERRORS, function, run

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_memory import validate
from check_stack import validate as validate_stack


def stack_tests():
    def row(size,kind='static'):return dict(function='test',bytes=size,kind=kind)
    assert validate_stack([row(2048),row(64)])[0]['bytes']==2048
    assert validate_stack([row(128,'dynamic,bounded')])
    for invalid in ([],[row(2049)],[row(64,'dynamic')]):
        try:validate_stack(invalid)
        except ValueError:pass
        else:raise AssertionError('invalid stack budget accepted')


def heap_tests():
    code = BASE + r'''
#include <stddef.h>
#include <errno.h>
static unsigned char arena[8192];
#define __heap_start arena
#define __heap_end (arena+sizeof(arena))
'''
    code += function('ufi_heap.c', '_sbrk')
    code += r'''
int main(void) {
    assert(_sbrk(0)==arena);
    assert(_sbrk(8192)==arena);
    assert(_sbrk(0)==arena+8192);
    errno=0;assert(_sbrk(1)==(void*)-1 && errno==ENOMEM);
    assert(_sbrk(0)==arena+8192);
    assert(_sbrk(-8192)==arena+8192);
    assert(_sbrk(0)==arena);
    assert(_sbrk(-1)==(void*)-1 && _sbrk(0)==arena);
    assert(_sbrk(PTRDIFF_MIN)==(void*)-1 && _sbrk(0)==arena);
    assert(_sbrk(PTRDIFF_MAX)==(void*)-1 && _sbrk(0)==arena);
    for(unsigned i=0;i<1024;i++)assert(_sbrk(8)==arena+i*8);
    assert(_sbrk(8)==(void*)-1 && _sbrk(0)==arena+8192);
    return 0;
}
'''
    run('regression_heap_bounds', code)


def psram_selection_tests():
    code = BASE + ERRORS + r'''
#define CHUNK_MAX_WORDS 32768u
#define FLUX_STORE_FALLBACK_WORDS (56u*1024u)
#define PSRAM_SIZE_BYTES (8u*1024u*1024u)
static uint32_t external[PSRAM_SIZE_BYTES/4], flux_fallback[FLUX_STORE_FALLBACK_WORDS];
#define OCTOSPI1_BASE ((uintptr_t)external)
static uint32_t *flux_store,*dma_scratch;
static uint32_t chunk_words,n_chunks,store_words;
static bool store_psram,reference_reserved;
'''
    code += function('ufi_flux.c', 'configure_store')
    code += function('ufi_flux.c', 'select_store')
    code += function('ufi_flux.c', 'ufi_flux_reference_reserve')
    code += function('ufi_flux.c', 'ufi_flux_reference')
    code += r'''
int main(void) {
    const bool outcomes[]={false,true,true,false,false,true};
    for(unsigned i=0;i<sizeof(outcomes)/sizeof(outcomes[0]);i++) {
        select_store(outcomes[i]);
        assert(store_psram==outcomes[i]);
        assert(flux_store==(outcomes[i]?external:flux_fallback));
        assert(store_words==(outcomes[i]?2064384u:38224u));
        uint32_t capacity=outcomes[i]?PSRAM_SIZE_BYTES/4:FLUX_STORE_FALLBACK_WORDS;
        assert(dma_scratch==flux_store+store_words);
        assert(store_words+chunk_words<=capacity);
        assert(chunk_words && !(chunk_words%8) && n_chunks>=2);
        flux_store[store_words-1]=0xfeedbeef;
        for(uint32_t k=0;k<chunk_words;k++)dma_scratch[k]=k;
        assert(flux_store[store_words-1]==0xfeedbeef);
    }
    select_store(true);uint32_t words;
    assert(!ufi_flux_reference_reserve(true));
    assert(ufi_flux_reference(&words)==external+PSRAM_SIZE_BYTES/8 && words==PSRAM_SIZE_BYTES/8);
    assert(store_words+chunk_words<=PSRAM_SIZE_BYTES/8); /* scratch never overlaps reference */
    uint32_t before=flux_invalidations;assert(!ufi_flux_reference_reserve(true) && flux_invalidations==before);
    assert(!ufi_flux_reference_reserve(false) && !ufi_flux_reference(&words) && !words);
    select_store(false);assert(ufi_flux_reference_reserve(true)==UFI_ERR_NOT_IMPL);
    return 0;
}
'''
    run('regression_psram_selection', code)


def budget_tests():
    sections = {'.dtcm':(0x20000000,0x18000),'.axi_sram':(0x24000400,224*1024),
                '.sram2':(0x30004000,8192)}
    symbols = {name:(value,0) for name,value in dict(
        __heap_start=0x24043000,__heap_end=0x24045000,_estack=0x20020000,
        _edtcm=0x20018000,__usb_rx_start=0x20000000,__usb_rx_end=0x20004000,
        _sbrk=0x08001000).items()}
    symbols.update(flux_fallback=(0x24000400,224*1024),write_ring=(0x30004000,8192))
    assert validate(sections,symbols)['heap_bytes']==8192
    for name,value in (
        ('__heap_end',(0x24050001,0)),
        ('__heap_start',(0x20000000,0)),
        ('_edtcm',(0x2001f000,0)),
        ('__usb_rx_end',(0x24004000,0)),
        ('flux_fallback',(0x20000000,224*1024)),
        ('flux_fallback',(0x24000400,128*1024)),
        ('write_ring',(0x30004001,8192)),
    ):
        bad=dict(symbols);bad[name]=value
        try:validate(sections,bad)
        except ValueError:pass
        else:raise AssertionError('invalid memory layout accepted: '+name)
    bad=dict(sections);bad['.unbudgeted']=(0x24048000,4096)
    try:validate(bad,symbols)
    except ValueError:pass
    else:raise AssertionError('unbudgeted AXI allocation accepted')
    print('memory budget rejection tests: passed')


if __name__=='__main__':
    stack_tests()
    heap_tests()
    psram_selection_tests()
    budget_tests()
