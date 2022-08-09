TARGET=main
LATEX=pdflatex
BIBTEX=bibtex

all: $(TARGET)

pdf: $(TARGET).pdf

$(TARGET): $(TARGET).pdf

$(TARGET).pdf: $(TARGET).tex $(TARGET).bib src/*.tex src/listing/*
	$(LATEX) $(TARGET).tex
	$(BIBTEX) $(TARGET)
	$(LATEX) $(TARGET).tex
	$(LATEX) $(TARGET).tex

clean:
	rm -f $(TARGET).pdf
	rm -f *.aux
	rm -f *.bbl
	rm -f *.blg
	rm -f *.dvi
	rm -f *.log
	rm -f *.lof
	rm -f *.lot
	rm -f *.toc

.PHONY: all clean pdf
