TARGET_THESIS=main
TARGET_SLIDE=slide
LATEX=pdflatex
BIBTEX=bibtex

all: $(TARGET_THESIS) $(TARGET_SLIDE)

pdf: $(TARGET_THESIS).pdf $(TARGET_SLIDE).pdf

$(TARGET_THESIS): $(TARGET_THESIS).pdf

$(TARGET_SLIDE): $(TARGET_SLIDE).pdf

$(TARGET_THESIS).pdf: $(TARGET_THESIS).tex \
               src/*.tex \
               src/*.bib \
               src/listing/* \
               src/figure/*
	$(LATEX) $(TARGET_THESIS).tex
	$(BIBTEX) $(TARGET_THESIS)
	$(LATEX) $(TARGET_THESIS).tex
	$(LATEX) $(TARGET_THESIS).tex

$(TARGET_SLIDE).pdf: $(TARGET_SLIDE).tex \
               src/figure/* \
               src/listing/*
	$(LATEX) $(TARGET_SLIDE).tex
	$(LATEX) $(TARGET_SLIDE).tex
	$(LATEX) $(TARGET_SLIDE).tex

clean:
	rm -f $(TARGET_THESIS).pdf
	rm -f $(TARGET_SLIDE).pdf
	rm -f *.aux
	rm -f *.bbl
	rm -f *.blg
	rm -f *.dvi
	rm -f *.log
	rm -f *.lof
	rm -f *.lot
	rm -f *.toc
	rm -f *.lol

.PHONY: all clean pdf
