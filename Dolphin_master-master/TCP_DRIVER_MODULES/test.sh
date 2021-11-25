IP_SRC=$(hostname -I);

disinfo -hostnames | while IFS= read -r line ; do
	if [ $line <> $IP_SRC ]; then
		IP_DEST=$line;
		echo "$IP_DEST";
		echo "$IP_SRC";
		break;
	fi;
done;

