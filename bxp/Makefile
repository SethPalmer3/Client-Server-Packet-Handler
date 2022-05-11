installheaders:
	if [ ! -d "/usr/local/include/BXP" ]; then mkdir /usr/local/include/BXP; chmod 755 /usr/local/include/BXP; fi
	cp bxp.h endpoint.h /usr/local/include/BXP
	chmod 644 /usr/local/include/BXP/*.h

installlibrary:
	cp libBXP.a /usr/local/lib
	chmod 755 /usr/local/lib/libBXP.a

installmanpages:
	if [ ! -d "/usr/share/man/man3bxp" ]; then mkdir /usr/share/man/man3bxp; chmod 755 /usr/share/man/man3bxp; fi
	cp doc/*.3bxp /usr/share/man/man3bxp
	chmod 644 /usr/share/man/man3bxp/*.3bxp
