#!/bin/bash

# drivers_link must be set to the url of the release  
drivers_link="https://dl.dolphinics.com/ci/16147/Dolphin_eXpressWare-Linux-x86_64-PX-28a8824496_ce05d9958c.ubuntu18.04.sh"

user_name="admin"
drivers_name="/home/$user_name/Documents/Dolphin_master/dolphin_installation/Dolphin_eXpressWare_drivers.sh"
new_uname="5\.4\.0\-050400\-generic"
git_email="vegar.reitan@gmail.com"
git_uname="reitanvegar"
nameserver="8\.8\.8\.8"  




#TODO: Should probably add gotoex<command> which will go to the folder which the executable is in path. 'which <command>' will give us the path 





# This function will install with --install-node
dis_install_node () {
    sudo sh $drivers_name --install-node 
}

dis_install () {
    sudo sh $drivers_name $@
}

# This function should download and install linux 5.4
# Must change grub file so it boots with 5.4 and next boot. (dis_change_boot_prio)
dis_install_linux_5_4(){

    cd /tmp/

    wget -c https://kernel.ubuntu.com/~kernel-ppa/mainline/v5.4/linux-headers-5.4.0-050400_5.4.0-050400.201911242031_all.deb
    wget -c https://kernel.ubuntu.com/~kernel-ppa/mainline/v5.4/linux-headers-5.4.0-050400-generic_5.4.0-050400.201911242031_amd64.deb
    wget -c https://kernel.ubuntu.com/~kernel-ppa/mainline/v5.4/linux-image-unsigned-5.4.0-050400-generic_5.4.0-050400.201911242031_amd64.deb
    wget -c https://kernel.ubuntu.com/~kernel-ppa/mainline/v5.4/linux-modules-5.4.0-050400-generic_5.4.0-050400.201911242031_amd64.deb

    sudo dpkg -i *.deb
}


# changes the boot priority to the new linux version
dis_change_boot_prio () {
    new_default="Advanced options for Ubuntu\>Ubuntu\, with Linux ${new_uname}"
    sudo sed -i 's/GRUB_DEFAULT\=.*/GRUB_DEFAULT\=\"'"$new_default"'\"/' /etc/default/grub
    sudo update-grub
}


# This function is removed as it would require sudo priveledges and that would not work from .bashrc 
dis_add_source_preinstall_to_bashrc () {
   echo "source ~/Documents/Dolphin_master/dolphin_installation/pre_install.sh" >> ~/.bashrc
}


dis_change_name_server () {
    sudo sed -i 's/nameserver.*/nameserver '"${nameserver}"'/' /etc/resolv.conf
}



# This function is removed as it would require sudo priveledges and that would not work from .bashrc 
#dis_add_change_name_server_to_bashrc () {
#   echo "sed -i 's/nameserver.*/nameserver ${nameserver}/' /etc/resolv.conf" >> ~/.bashrc
#}

# This function should install with --get-tarball
dis_install_tarball () {	
    sudo sh $drivers_name --get-tarball
}

# This function should download the the
dis_download_PX (){
    usage_string="Usage: dis_download_PX <username> <password>"

    if [[ $# != 2 ]]; then
        echo $usage_string  
	return 0
    fi

    wget -O $drivers_name --user ${1} --password ${2} $drivers_link 
}



# This function will install some handy programs(no needed), get updates and other stuff 
# in order to fullfill the prerequisites of the DIS installation. 
# Assumes a clean ubuntu 18.04.5 LTS image. #TODO change to 21.04
dis_install_essentials () {

    usage_string="Usage: dis_install_essentials [<username> <password>]. If username and password is passed then it will download the dolphin PXH drivers "

    if [[ $# == 1 && $1 == "-h" ]]; then
        echo $usage_string  
	return 0
    fi

    if [[ $# != 2 && $# != 0 ]]; then
        echo $usage_string
	return 0
    fi
	
    
    # Set vim tab to 4 spaces.  
    echo "set tabstop=4" >> ~/.bashrc
    
	
    # Set git stuff
    git config --global user.email $git_email
    git config --global user.name $reitanvegar

    # Does som updates (Code gotten from Dolphinics/Docker)
    apt-get update 
    apt-get -y install autoconf make libtool gcc g++ libqt4-dev pkg-config linux-headers-generic build-essential git

    # install some hand stuff 
    apt install tree
    apt install nmap

    # Installs Vim and VS-Code 
    echo "Y" | sudo apt install vim
    #sudo snap install --classic code    
    
    # Downloads the Dolphin hardware. 
    if [[ $# == 2 ]]; then
        dis_download_PX $@
    fi
}

#TODO: should add a function here that changes or adds "PasswordAuthentication no" in the /etc/ssh/sshd_config file
# 

dis_add_executables_to_path () {
    export PATH="/opt/DIS/sbin:$PATH"
    export PATH="/etc/init.d:$PATH"
    export PATH="/opt/DIS/bin:$PATH"
}


dis_netconfig () {
    sudo /opt/DIS/sbin/dis_netconfig  $@
}

dis_mkconf () {
    sudo /opt/DIS/sbin/dis_mkconf  $@
}

# start all services.
dis_services_start () {
    sudo /opt/DIS/sbin/dis_services start 
}

dis_services_restart () {
    sudo /opt/DIS/sbin/dis_services restart 
}


# just some setup and start commands
dis_kosif_setup() {
    sudo /opt/DIS/sbin/kosif-setup -i
}

dis_px_setup() {
    sudo /opt/DIS/sbin/px-setup -i
}

dis_irm_setup() {
    sudo /opt/DIS/sbin/irm-setup -i
}

dis_sisci_setup () {
    sudo /opt/DIS/sbin/sisci-setup -i
}


dis_kosif_start () {
    sudo /etc/init.d/dis_kosif start 
}

dis_px_start () {
    sudo /etc/init.d/dis_px start 
}

dis_irm_start () {
    sudo /etc/init.d/dis_irm start
}

dis_sisci_start () {
    sudo /etc/init.d/dis_sisci start 
}

dis_set_debug_flags () {
    usage="Usage: dis_set_debug_flags [List of bits to set. Example: \"18 32\"]. If no bits are specified we use default bits: $default_bits. Use \"c\" to clear bits. "
    default_bits="18 32"
    if [[ $# == 0 ]]; then
        dis_tool set-debug 0 "${default_bits}"
    elif [[ $# == 1 ]]; then
        if [[ $1 == '-h' ]]; then
            echo $usage    
        else
            dis_tool set-debug 0 "${1}"
        fi
    else
        echo $usage
    fi
}

dis_debug_ntb () {
    sudo bash -c 'echo 1 > /sys/module/dis_px_ntb/parameters/ntb_debug_level'
}

dis_debug_ntb_extra () {
    sudo bash -c 'echo 0 > /sys/module/dis_px_ntb/parameters/ntb_debug_level'
}


dis_debug () {
    dis_debug_ntb
    dis_set_debug_flags
}

dis_debug_extra () {
    dis_debug_ntb_extra
    dis_set_debug_flags
}

# This function should fetch last update from git, compile it and insert it. 
ltcp_install () {

    # Just make sure it is set before utilizing any of the dis_* functions.
    dis_add_executables_to_path

    usage="usage: ltcp_install [flags]. -h for help. -d for debug which will set default debug bits and activate ntb debugging. -de will do same as -d, but with more debug info. "
    
    if [[ $# > 1 ]]; then
        echo $usage
    fi

    if [[ $# == 1 ]]; then
        if [[ $1 == "-h" ]]; then
            echo $usage
            return 0
        elif [[ $1 == "-d" ]]; then
            echo "Running with debug."
            dis_debug
        elif [[ $1 == "-de" ]]; then
            echo "Running with debug extra."
            dis_debug_extra
        else
            echo $usage
            return -1
        fi
    fi

	cd /home/$USER/Documents/Dolphin_master/TCP_DRIVER_MODULES
	sudo git pull
	if [ $? -ne 0 ]; then
			echo "Failed to pool from git."
			return -1
	fi
	
	sudo make

	if [ $? -ne 0 ]; then
			echo "Failed to make module."
			return -1
	fi

	sudo make insert_module
	if [ $? -ne 0 ]; then
			echo "Failed to insert module."
			return -1
	fi

	return 0
}

ltcp_server () {
    pushd $(pwd)
    cd /home/$USER/Documents/Dolphin_master/TCP_DRIVER_MODULES/userspace_test/write_file/
    sudo make clean 
    sudo make
        
	sudo ./server $1
    popd
}

ltcp_client () {

    pushd $(pwd)
    cd /home/$USER/Documents/Dolphin_master/TCP_DRIVER_MODULES/userspace_test/write_file/
    sudo make clean 
    sudo make

    if [ $(hostname -I | awk '{print $1}') = "81.175.23.17" ]; then
	    sudo ./client 81.175.23.16 $1
    else
	    sudo ./client 81.175.23.17 $1
    fi
    popd
}


print_err_stuff () {
    component=$1

    bold=$(tput bold)
    normal=$(tput sgr0)
    RED='\033[0;31m'
    #NC='\033[0m'
    printf "\n\n\n"

    printf "${bold}${RED}systemctl status dis_${component}.service: ${normal}\n\n"    
    systemctl status dis_${component}.service | tail -n 10
    printf "\n\n\n"

    printf "${bold}${RED}journalctl -xe: ${normal}\n\n"    
    journalctl -xe | tail -n 10
    printf "\n\n\n"

    printf "${bold}${RED}dmesg: ${normal}\n\n"    
    dmesg | tail -n 10
    printf "\n\n\n"

    printf "${bold}${RED}/var/log/dis_${component}.log ${normal}\n\n"
    cat /var/log/dis_${component}.log | tail -n 10
    printf "\n\n"
}

# print error stuff kosif
dis_kosif_err_out () {
    print_err_stuff kosif
}

# print error stuff px 
dis_px_err_out () {
    print_err_stuff px
}

# print error stuff irm
dis_irm_err_out () {
    print_err_stuff irm
}

# print error stuff sisci
dis_sisci_err_out () {
    print_err_stuff sisci
}

# Will give the give tree for the given path. 
# Must be a subdir of current directory .
# TODO: check args
find_path () {
    tree -P "${1}" --prune 
}

grub_change_default() {
    sudo vim /etc/default/grub
}

grub_update_default() {
    sudo update-grub
}

easypush () {
    sudo git add -A && sudo git commit -m  "This is a standard debug message."  && sudo git push
}