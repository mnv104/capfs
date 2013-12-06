#
# Copyright (C) 2004
#  - Murali Vilayannur

BEGIN{
	count=0;
	parse_error = 0;
}
# Ignore Lines that begin with a # as comments
/ */ {
	if (substr($1,1,1) != "#" && NF > 0) {
		if(NF > 2) {
			parse_error = 1;
		}
		hosts[count++]=$1;
	}
} 

END  {
	if(parse_error == 0) {
		for(i=0;i < count; i++) {
			printf "%s\n", hosts[i] >> "machinefile";
		}
		printf "%d\n", count;
	}
	else {
		printf "-1\n";
	}
}
