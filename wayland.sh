#!/bin/bash

# Formatting helpers
function comment {
	echo "/* $* */"
}

function banner {
	echo "/***********************************************************"
	echo " * $1"                                                  
	echo " ***********************************************************/"
}

function enum {
	printf "\t%-30s = %s,\n" "$(upper $1)" "$2"
}

function upper {
	echo $1 | tr 'a-z' 'A-Z'
}

# XPath Helpers
function xpath {
	xmllint --xpath $1 wayland.xml
}

function xpathv {
	xpath $1 | sed -r 's/\w+="([^"]+)"/\1/g'
}

function exists {
	xpath "$1" > /dev/null 2>&1
}

function isarray {
	exists "$1/arg[@type='string']|$1/arg[@type='array']|$1/arg[@type='fd']"
}

# Message helpers
function msg_field {
	case $1 in
		int)    echo -e "\tint32_t     $2;"  ;;
		uint)   echo -e "\tuint32_t    $2;"  ;;
		new_id) echo -e "\tuint32_t    $2;"  ;;
		object) echo -e "\tuint32_t    $2;"  ;;
		fixed)  echo -e "\twl_fixed_t  $2;"  ;;
		array)  echo -e "\twl_array_t  $2;"  ;;
		string) echo -e "\twl_string_t $2;"  ;;
		fd)     ;;
	esac
}

function msg_array {
	for t in $(xpathv "$1"); do
		case $t in
			array)  echo -n "a" ;;
			string) echo -n "s" ;;
			fd)     echo -n "f" ;;
			*)      echo -n "-" ;;
		esac
	done
}

# Interface functions
function print_iface_vers {
	local vers
	for i in $IFACES; do
		vers=$(xpath "string(/protocol/interface[@name='$i']/@version)")
		printf "#define %-32s %s\n" "$(upper ${i}_version)" "$vers"
	done
}

function print_iface_enum {
	local cnt
	cnt=0
	for i in $IFACES; do
		enum "${i}" $((cnt++))
	done
	enum "wl_num_interfaces" $((cnt++))
}

# Enumeration functions
function print_enums {
	local base k v
	base="/protocol/interface[@name='$i']/enum"
	if exists "$base"; then
		comment $1
		for e in $(xpathv "$base/@name"); do
			echo "typedef enum {"
			for n in $(xpathv "$base[@name='$e']/entry/@name"); do
				k="$base[@name='$e']/entry[@name='$n']/@value"
				v=$(xpath "string($k)")
				printf "\t%-40s = %s,\n" \
					"$(upper "${i}_${e}_${n}")" "$v"
			done
			echo "} ${i}_${e};"
			echo
		done
	fi
}

# Message functions
function print_msg_ids {
	local cnt base
	cnt=0
	base="/protocol/interface[@name='$IFACE']/$2"
	if exists "$base"; then
		comment $1
		echo "typedef enum {"
		for r in $(xpathv "$base/@name"); do
			enum "${IFACE}_${r}" $((cnt++))
		done
		enum "${IFACE/wl/wl_num}_${2}s" $((cnt++))
		echo "} ${IFACE}_${2}_t;"
		echo
	fi
}

function print_msg_defs {
	local base msgs args k t
	base="/protocol/interface[@name='$IFACE']/$2"
	if exists "$base/arg"; then
		comment $1
		msgs=$(xpathv "$base/@name")
		for m in $msgs; do
			if exists "$base[@name='$m']/arg"; then
				echo "typedef struct {"
				args=$(xpathv "$base[@name='$m']/arg/@name")
				for a in $args; do
					k="$base[@name='$m']/arg[@name='$a']/@type"
					t=$(xpath "string($k)")
					msg_field "$t" "$a"
				done
				echo "} ${IFACE}_${m}_t;"
				echo
			fi
		done
	fi
}

function print_msg_union {
	local base t n
	for IFACE in $IFACES; do
		base="/protocol/interface[@name='$IFACE']/$1"
		if exists "$base"; then
			for r in $(xpathv "$base/@name"); do
				if exists "$base[@name='$r']/arg"; then
					t="${IFACE}_${r}_t"
					n="${IFACE}_${r/wl_/}"
					printf '\t%-50s %s;\n' "$t" "$n"
				fi
			done
		fi
	done
}

function print_msg_table {
	local base size idx str
	for IFACE in $IFACES; do
		base="/protocol/interface[@name='$IFACE']/$1"
		size="$(upper "${IFACE/wl/wl_num}_${1}s")"
		if isarray "$base"; then
			echo -e "\t[$(upper $IFACE)] (const char *[$size]) {"
			for r in $(xpathv "$base/@name"); do
				if isarray "$base[@name='$r']"; then
					idx="$(upper "${IFACE}_${r}")"
					str="$(msg_array "$base[@name='$r']/arg/@type")"
					printf '\t\t%-30s "%s",\n' "[$idx]" "$str"
				fi
			done
			echo -e "\t},"
		fi
	done
}

# Main
IFACES=$(xpathv "/protocol/interface/@name")

cat <<EOF
#ifndef WAYLAND_H
#define WAYLAND_H

#include <stdint.h>

$(banner "Common Types")

#define WL_MESSAGE_LEN 4096

typedef struct {
	uint32_t id;
	uint32_t len : 16;
	uint32_t op  : 16;
} wl_header_t;

typedef struct {
	uint32_t len;
	char    *str;
} wl_string_t;

typedef struct {
	uint32_t len;
	void    *data;
} wl_array_t;

typedef struct {
	uint32_t num  : 24;
	uint32_t frac : 8;
} wl_fixed_t;

$(banner "Interfaces")

/* Interface Versions */
$(print_iface_vers)

/* Interface IDs */
typedef enum {
$(print_iface_enum)
} wl_interface_t;

$(for IFACE in $IFACES; do
	banner "Interface $(upper $IFACE)"
	echo
	print_msg_ids  "Request IDs"       request
	print_msg_ids  "Event IDs"         event
	print_enums    "Enumerations"      
	print_msg_defs "Requests Messages" request
	print_msg_defs "Events Messages"   event
done)

/* Union messages */
typedef union {
$(print_msg_union request)
} wl_request_t;

typedef union {
$(print_msg_union event)
} wl_event_t;

$(banner "Arrays and Strings")

/* Constants */
#define WL_ARRAY_NONE   '-'
#define WL_ARRAY_STRING 's'
#define WL_ARRAY_ARRAY  'a'
#define WL_ARRAY_FD     'f'

extern const char **wl_rarray[WL_NUM_INTERFACES];
extern const char **wl_earray[WL_NUM_INTERFACES];

/* Request Array */
#ifdef WL_DEFINE_TABLES
const char **wl_rarray[WL_NUM_INTERFACES] = {
$(print_msg_table request)
};
#endif

/* Event Array */
#ifdef WL_DEFINE_TABLES
const char **wl_earray[WL_NUM_INTERFACES] = {
$(print_msg_table event)
};
#endif

#endif
EOF
