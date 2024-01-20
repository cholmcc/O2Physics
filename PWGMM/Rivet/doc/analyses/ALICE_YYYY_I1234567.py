#!/usr/bin/env -S python -i --

def plotit(infile,raw,save):
    from matplotlib.pyplot import gca
    from yoda import readYODA
    from sys import stderr

    aos = readYODA(infile)

    if aos is None:
        raise RuntimeError(f'No AOS read from {infile}')

    histo = None
    nev   = None
    xsec  = None

    for prefix in ["", "/RAW"]:
        histo = aos.get(prefix+'/ALICE_YYYY_I1234567/d01-x01-y01',None)
        nev   = aos.get(prefix+'/_EVTCOUNT',None)
        xsec  = aos.get(prefix+'/_XSEC',None)
        if histo is not None and \
           histo.effNumEntries() > .1 and \
           nev is not None and \
           xsec is not None:
            break

    if histo is None:
        raise RuntimeError(f'Histogram not found in {infile}')

    ax = gca()
    ax.errorbar(histo.xMids(),
                histo.yVals(),
                histo.yErrs(),
                histo.xWidths(),
                'o')
    ax.set_xlabel(r'$\eta$')
    ax.set_ylabel(r'$\mathrm{d}N_{\mathrm{ch}}/\mathrm{d}\eta$')
    ax.set_title(f'{int(nev.val())} events '
                 f'{"("+prefix+")" if len(prefix)>0 else ""}')

    ax.figure.show()
    ax.figure.tight_layout()

    if save:
        from pathlib import Path

        inpath = Path(infile.name)
        pngpath = inpath.with_suffix('.png')

        ax.figure.savefig(str(pngpath))


if __name__ == '__main__':
    from argparse import ArgumentParser, FileType

    ap = ArgumentParser(description='Plot results')
    ap.add_argument('input',nargs='?',default='AO2D_LHC23d1f_520259_001.yoda',
                    help='Input file',
                    type=FileType('r'))
    ap.add_argument('-r','--raw',action='store_true',
                    help='Show raw results')
    ap.add_argument('-s','--save',action='store_true',
                    help='Save plot to image file')

    try:
        args = ap.parse_args()
        plotit(args.input,args.raw,args.save)
    except Exception as e:
        print(e)

    print(f'Type Ctrl-D or write exit() to end')

#
# EOF
#
