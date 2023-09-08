build/WebTemplates.h: $(shell find src/apps/web/tmpls/ -type f -name '*.tmpl')
	perl golpe/external/templar/templar.pl src/apps/web/tmpls/ tmpl $@

src/apps/web/WebReader.o: build/WebTemplates.h build/WebStaticFiles.h

LDLIBS += -lre2




build/web-static/oddbean.css: src/apps/web/static/reset.css src/apps/web/static/oddbean.css
	mkdir -p build/web-static/
	cat $^ | sassc -s -t compressed > $@

build/web-static/oddbean.js: src/apps/web/static/base.ts src/apps/web/static/turbo.js src/apps/web/static/oddbean.js src/apps/web/static/alpine.js
	mkdir -p build/web-static/
	cat $^ | esbuild --loader=ts --minify > $@

build/web-static/oddbean.svg: src/apps/web/static/oddbean.svg
	cp $^ $@

build/web-static/favicon.ico: src/apps/web/static/favicon.ico
	cp $^ $@

build/WebStaticFiles.h: build/web-static/oddbean.css build/web-static/oddbean.js build/web-static/oddbean.svg build/web-static/favicon.ico
	perl golpe/external/hoytech-cpp/dirToCppHeader.pl build/web-static/ oddbeanStatic > $@