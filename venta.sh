#!/usr/bin/env bash
if [[ "${1:-}" == "-V" || "${1:-}" == "--version" ]]; then
  echo "${VENTA_VERSION:-dev}"
  exit 0
fi

if [[ "${1:-}" == "-U" || "${1:-}" == "--update" ]]; then
  current_ver="${VENTA_VERSION:-dev}"
  if [[ "$current_ver" == "dev" ]]; then
    echo "use the flakes install u bum" #dev stuff if i forget like a idiot
    exit 0
  fi

  echo "Checking for updates"

  fetch_release() {
    local api_url="$1"
    local json
    json=$(curl -s --max-time 5 "$api_url" 2>/dev/null)

    if [[ -n "$json" ]] && echo "$json" | grep -q '"tag_name"'; then
      if command -v jq >/dev/null; then
        echo "$json" | jq -r '.tag_name' 2>/dev/null
      else
        echo "$json" | grep -o '"tag_name":"[^"]*"' | head -n1 | sed 's/"tag_name":"//;s/"//'
      fi
    fi
  }

  tag=$(fetch_release "https://api.github.com/repos/realnrxg/venta/releases/latest")

  if [[ -z "$tag" ]]; then
    echo "Github is unreachable trying codeberg"
    tag=$(fetch_release "https://codeberg.org/api/v1/repos/nrxg/venta/releases/latest")
  fi

  if [[ -z "$tag" ]]; then
    echo "Could not reach github or codeberg"
    exit 1
  fi

  tag_clean="${tag#v}"
  current_clean="${current_ver#v}"

  if [[ "$tag_clean" == "$current_clean" ]]; then
    echo "No new updates found ($tag)."
  else
    latest_is_newer=$(printf '%s\n%s' "$current_clean" "$tag_clean" | sort -V | tail -n1)
    if [[ "$latest_is_newer" == "$tag_clean" ]]; then
      echo "Update available: $current_ver → $tag"
    else
      echo "You're on a newer version (ur not me u bum) ($current_ver > $tag)."
    fi
  fi
  exit 0
fi

set -u

hex_to_ansi() {
  local hex="${1#\#}"
  local r g b
  r=$(( 16#${hex:0:2} ))
  g=$(( 16#${hex:2:2} ))
  b=$(( 16#${hex:4:2} ))
  printf '\033[38;2;%d;%d;%dm' "$r" "$g" "$b"
}

hex_dim() {
  local hex="${1#\#}" pct=$2
  local r g b
  r=$(( 16#${hex:0:2} * pct / 100 ))
  g=$(( 16#${hex:2:2} * pct / 100 ))
  b=$(( 16#${hex:4:2} * pct / 100 ))
  (( r > 255 )) && r=255; (( g > 255 )) && g=255; (( b > 255 )) && b=255
  printf '\033[38;2;%d;%d;%dm' "$r" "$g" "$b"
}

CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/venta"
CONFIG_FILE="$CONFIG_DIR/config.json"

if [[ ! -f "$CONFIG_FILE" ]]; then
  mkdir -p "$CONFIG_DIR"
  cat > "$CONFIG_FILE" << 'VENTA_CONFIG'
{
  "dna_color": "#00ffcc",
  "corrupt_color": "#ff0000",
  "recover_color": "#00ff50",
  "fps": 30,
  "show_stats": true
}
VENTA_CONFIG
fi

DNA_HEX="00ffcc"
CORRUPT_HEX="ff0000"
RECOVER_HEX="00ff50"
FPS=30
SHOW_STATS=true

if [[ -f "$CONFIG_FILE" ]]; then
  _dna=$(awk -F'"' '/"dna_color"/{gsub(/[^0-9a-fA-F]/,"",$4); print $4}' "$CONFIG_FILE" 2>/dev/null)
  _cor=$(awk -F'"' '/"corrupt_color"/{gsub(/[^0-9a-fA-F]/,"",$4); print $4}' "$CONFIG_FILE" 2>/dev/null)
  _rec=$(awk -F'"' '/"recover_color"/{gsub(/[^0-9a-fA-F]/,"",$4); print $4}' "$CONFIG_FILE" 2>/dev/null)
  _fps=$(awk -F'[":,}]' '/"fps"/{for(i=1;i<=NF;i++) if($i+0>=30&&$i+0<=144){print int($i+0);exit}}' "$CONFIG_FILE" 2>/dev/null)
  [[ -n "$_dna" && ${#_dna} -eq 6 ]] && DNA_HEX="$_dna"
  [[ -n "$_cor" && ${#_cor} -eq 6 ]] && CORRUPT_HEX="$_cor"
  [[ -n "$_rec" && ${#_rec} -eq 6 ]] && RECOVER_HEX="$_rec"
  [[ -n "$_fps" ]] && FPS="$_fps"
  _show=$(awk -F'"' '/"show_stats"/{print $4}' "$CONFIG_FILE" 2>/dev/null)
  [[ "$_show" == "false" ]] && SHOW_STATS=false
fi

FRAME_SLEEP=$(awk "BEGIN{printf \"%.6f\", 1/$FPS}")

COL_DNA_DIM=$(hex_dim    "$DNA_HEX" 28)
COL_DNA_MID=$(hex_dim    "$DNA_HEX" 62)
COL_DNA_BRIGHT=$(hex_to_ansi "$DNA_HEX")
COL_CORRUPT=$(hex_to_ansi "$CORRUPT_HEX")
COL_RECOVER=$(hex_to_ansi "$RECOVER_HEX")
COL_RECOVER_MID=$(hex_dim "$RECOVER_HEX" 55)
COL_RECOVER_DIM=$(hex_dim "$RECOVER_HEX" 25)
COL_RESET=$'\033[0m'

COL_REFORM=$(awk -v d="$DNA_HEX" -v c="$CORRUPT_HEX" 'BEGIN{
  r=int((int("0x"substr(d,1,2))+int("0x"substr(c,1,2)))/2)
  g=int((int("0x"substr(d,3,2))+int("0x"substr(c,3,2)))/2)
  b=int((int("0x"substr(d,5,2))+int("0x"substr(c,5,2)))/2)
  printf "\033[38;2;%d;%d;%dm",r,g,b
}')

PULSE_STEPS=20
mapfile -t PULSE_BRIGHT < <(awk -v d="$DNA_HEX" -v c="$CORRUPT_HEX" -v n="$PULSE_STEPS" 'BEGIN{
  dr=int("0x"substr(d,1,2));dg=int("0x"substr(d,3,2));db=int("0x"substr(d,5,2))
  cr=int("0x"substr(c,1,2));cg=int("0x"substr(c,3,2));cb=int("0x"substr(c,5,2))
  for(t=0;t<n;t++){printf "\033[38;2;%d;%d;%dm\n",int(dr+(cr-dr)*t/(n-1)),int(dg+(cg-dg)*t/(n-1)),int(db+(cb-db)*t/(n-1))}
}')
mapfile -t PULSE_MID < <(awk -v d="$DNA_HEX" -v c="$CORRUPT_HEX" -v n="$PULSE_STEPS" 'BEGIN{
  dr=int(int("0x"substr(d,1,2))*62/100);dg=int(int("0x"substr(d,3,2))*62/100);db=int(int("0x"substr(d,5,2))*62/100)
  cr=int(int("0x"substr(c,1,2))*62/100);cg=int(int("0x"substr(c,3,2))*62/100);cb=int(int("0x"substr(c,5,2))*62/100)
  for(t=0;t<n;t++){printf "\033[38;2;%d;%d;%dm\n",int(dr+(cr-dr)*t/(n-1)),int(dg+(cg-dg)*t/(n-1)),int(db+(cb-db)*t/(n-1))}
}')
mapfile -t PULSE_DIM < <(awk -v d="$DNA_HEX" -v c="$CORRUPT_HEX" -v n="$PULSE_STEPS" 'BEGIN{
  dr=int(int("0x"substr(d,1,2))*28/100);dg=int(int("0x"substr(d,3,2))*28/100);db=int(int("0x"substr(d,5,2))*28/100)
  cr=int(int("0x"substr(c,1,2))*28/100);cg=int(int("0x"substr(c,3,2))*28/100);cb=int(int("0x"substr(c,5,2))*28/100)
  for(t=0;t<n;t++){printf "\033[38;2;%d;%d;%dm\n",int(dr+(cr-dr)*t/(n-1)),int(dg+(cg-dg)*t/(n-1)),int(db+(cb-db)*t/(n-1))}
}')

cleanup() {
  printf '\033[0m\033[?25h\033[?1049l' 2>/dev/null || true
  stty echo 2>/dev/null || true
  stty sane 2>/dev/null || true
}
running=1
trap 'running=0' INT TERM
trap cleanup EXIT

printf '\033[?1049h\033[?25l\033[2J'
stty -echo -icanon min 0 time 1 2>/dev/null || true

read -r rows cols < <(stty size 2>/dev/null || printf '24 80')
(( rows < 20 )) && rows=20
(( cols < 50 )) && cols=50

prev_rows=$rows
prev_cols=$cols

center=$((rows / 2))
amp=$((rows / 4))
(( amp < 4 )) && amp=4

resize_arrays() {
  center=$((rows / 2))
  amp=$((rows / 4))
  (( amp < 4 )) && amp=4
  total=$(( rows * cols ))
  chars=()
  cell_type=()
  for ((i=0;i<total;i++)); do chars[$i]=' '; cell_type[$i]=1; done
  init_arrays "$cols"
  return 0
}

resize_handler() {
  read -r rows cols < <(stty size 2>/dev/null || printf '24 80')
  (( rows < 20 )) && rows=20
  (( cols < 50 )) && cols=50
  [[ "$rows" -eq "$prev_rows" && "$cols" -eq "$prev_cols" ]] && return 1
  prev_rows=$rows
  prev_cols=$cols
  resize_arrays
}

mapfile -t SIN < <(awk 'BEGIN{for(i=0;i<360;i++)printf "%d\n",int(sin(i*3.141592653589793/180)*1000)}')
mapfile -t COS < <(awk 'BEGIN{for(i=0;i<360;i++)printf "%d\n",int(cos(i*3.141592653589793/180)*1000)}')

bases=(A T C G)
comp=(T A G C)
rungs=('─' '─' '═' '─' '─' '╌' '─' '═' '─' '╌')
corrupt=('#' '?' '@' '%' '&' 'X' '!' '/' 'S' '\\')
shards=('·' '*' '+' 'x' '~')
crystal=('◆' '◈' '◇' '◊' '▪' '▫' '◉' '○')

tick=0
BREAK_TOTAL=18
BREAK_SNAP=5

glitch_timer=0
glitch_center=0
glitch_strength=0
glitch_pulse=0

recover_active=0
recover_x=0
recover_width=8
recover_cooldown=0

collapse_state=0
collapse_timer=0
COLLAPSE_DISSOLVE=45
COLLAPSE_CHAOS=55
COLLAPSE_REFORM=90
collapse_amp=$amp
reform_progress=0

POST_REFORM_COOLDOWN=350
post_reform_cooldown=0

base_idx=()
decay=()
break_timer=()

chars=()
cell_type=()

put_char() {
  local y=$1 x=$2 ch=$3 ctype=$4
  (( y < 0 || y >= rows || x < 0 || x >= cols )) && return
  local idx=$(( y * cols + x ))
  chars[$idx]="$ch"
  cell_type[$idx]=$ctype
}

init_arrays() {
  local n=$1 i
  for ((i=0;i<n;i++)); do
    [[ -z "${base_idx[i]+x}"    ]] && base_idx[i]=$((RANDOM % 4))
    [[ -z "${decay[i]+x}"       ]] && decay[i]=0
    [[ -z "${break_timer[i]+x}" ]] && break_timer[i]=0
  done
}

init_arrays "$cols"
total=$(( rows * cols ))

assemble_radius=0
while (( running && assemble_radius <= cols/2+1 )); do
  resize_handler || true
  center=$((rows / 2))
  amp=$((rows / 4))
  (( amp < 4 )) && amp=4

  a_out=''
  for ((i=0;i<rows;i++)); do
    a_out+=$'\033'; a_out+="[$((i+1));1H"
    for ((j=0;j<cols;j++)); do
      dist_from_c=$(( j - cols/2 ))
      (( dist_from_c < 0 )) && dist_from_c=$(( -dist_from_c ))

      if (( dist_from_c > assemble_radius )); then
        a_out+=' '
        continue
      fi

      phase=$(( (assemble_radius*7 + j*9) % 360 ))
      s=${SIN[$phase]}
      cosv=${COS[$phase]}
      y1=$(( center + (s*amp/1000) ))
      y2=$(( center - (s*amp/1000) ))
      drift=$(( SIN[$(((phase+90)%360))] * 2 / 1000 ))
      y1=$(( y1 + drift ))
      y2=$(( y2 - drift ))

      top=$y1
      bot=$y2
      (( top > bot )) && { tmp=$top; top=$bot; bot=$tmp; }

      mid_a=$(( (top + bot) / 2 ))
      edge_dist=$(( assemble_radius - dist_from_c ))

      if   (( edge_dist <= 2 )); then
        a_col="$COL_DNA_BRIGHT"
      elif (( edge_dist <= 5 )); then
        a_col="$COL_DNA_MID"
      else
        a_col="$COL_DNA_DIM"
      fi

      if   (( cosv > 600 )); then depth=2
      elif (( cosv > -600 )); then depth=1
      else                         depth=0; fi

      if (( i == y1 || i == y2 )); then
        a_out+="${a_col}${bases[${base_idx[$j]}]}"
        if (( depth >= 1 && i == y1 && i-1 >= 0 )); then
          a_out+=$'\033'; a_out+="[$((i));${j}H"
          a_out+="${COL_DNA_MID}·"
        fi
        if (( depth >= 1 && i == y2 && i+1 < rows )); then
          a_out+=$'\033'; a_out+="[$((i+2));${j}H"
          a_out+="${COL_DNA_MID}·"
        fi
      elif (( i > top && i < bot )); then
        dist_from_mid=$(( i - mid_a ))
        (( dist_from_mid < 0 )) && dist_from_mid=$(( -dist_from_mid ))

        rung_phase=$(( (j + assemble_radius/2) % 5 ))

        if (( rung_phase == 0 )); then
          if (( dist_from_mid == 0 )); then
            ch='═'
          elif (( dist_from_mid == 1 && (bot-top-1) > 3 )); then
            ch='│'
          else
            ch='·'
          fi
        else
          if (( dist_from_mid <= 1 )); then
            case $(((j+i+assemble_radius)%4)) in
              0) ch='·' ;; 1) ch=',' ;; 2) ch='`' ;; *) ch='.' ;;
            esac
          else
            if (( (j+i+assemble_radius)%7 == 0 )); then
              ch='·'
            else
              ch=' '
            fi
          fi
        fi

        if [[ "$ch" != ' ' ]]; then
          a_out+="${a_col}${ch}"
        else
          a_out+=' '
        fi
      else
        a_out+=' '
      fi
    done
    a_out+="$COL_RESET"
  done

  printf '%s' "$a_out"
  assemble_radius=$(( assemble_radius + 3 ))
  sleep "$FRAME_SLEEP"
done

while (( running )); do
  resize_handler || true
  center=$((rows / 2))
  amp=$((rows / 4))
  (( amp < 4 )) && amp=4
  total=$(( rows * cols ))

  key=""
  IFS= read -r -t 0.01 -n 1 key 2>/dev/null || true
  case "$key" in
    r|R) recover_active=1; recover_x=0; recover_cooldown=0 ;;
  esac

  for ((i=0;i<total;i++)); do chars[$i]=' '; cell_type[$i]=1; done

  if (( collapse_state == 0 )); then
    if (( tick > 1000 && post_reform_cooldown == 0 )); then
      crit_cols=0
      for ((x=0;x<cols;x++)); do (( decay[x] >= 27 )) && crit_cols=$(( crit_cols+1 )); done
      if (( crit_cols * 100 / cols > 85 )); then
        collapse_state=1
        collapse_timer=$COLLAPSE_DISSOLVE
        collapse_amp=$amp
        recover_active=0; recover_cooldown=9999
      fi
    fi

  elif (( collapse_state == 1 )); then
    collapse_timer=$(( collapse_timer - 1 ))
    collapse_amp=$(( amp * collapse_timer / COLLAPSE_DISSOLVE ))
    (( collapse_amp < 0 )) && collapse_amp=0
    for ((x=0;x<cols;x++)); do
      decay[$x]=$(( decay[x] + 3 ))
      (( decay[x] > 30 )) && decay[x]=30
    done
    if (( collapse_timer <= 0 )); then
      collapse_state=2
      collapse_timer=$COLLAPSE_CHAOS
      collapse_amp=0
    fi

  elif (( collapse_state == 2 )); then
    collapse_timer=$(( collapse_timer - 1 ))
    chaos_density=$(( 40 + RANDOM % 40 ))
    for ((ci=0;ci<rows;ci++)); do
      for ((cj=0;cj<cols;cj++)); do
        if (( RANDOM % 100 < chaos_density )); then
          ch=${corrupt[$((RANDOM % ${#corrupt[@]}))]}
          put_char "$ci" "$cj" "$ch" 3
        fi
      done
    done
    if (( collapse_timer <= 0 )); then
      collapse_state=3
      collapse_timer=$COLLAPSE_REFORM
      collapse_amp=0
      reform_progress=0
      for ((x=0;x<cols;x++)); do decay[$x]=0; break_timer[$x]=0; done
      recover_cooldown=600
    fi

  elif (( collapse_state == 3 )); then
    collapse_timer=$(( collapse_timer - 1 ))
    reform_progress=$(( COLLAPSE_REFORM - collapse_timer ))
    collapse_amp=$(( amp * reform_progress / COLLAPSE_REFORM ))
    (( collapse_amp > amp )) && collapse_amp=$amp
    if (( collapse_timer <= 0 )); then
      collapse_state=0
      collapse_amp=$amp
      post_reform_cooldown=$POST_REFORM_COOLDOWN
    fi
  fi

  if (( collapse_state == 0 && glitch_timer == 0 && tick > 40 && post_reform_cooldown == 0 )); then
    if (( RANDOM % 1000 < 7 + tick/160 )); then
      glitch_timer=$((8 + RANDOM%10))
      glitch_center=$((RANDOM%cols))
      glitch_strength=$((2 + RANDOM%5))
      glitch_pulse=$PULSE_STEPS
    fi
  fi
  (( glitch_pulse > 0 )) && glitch_pulse=$(( glitch_pulse-1 ))

  (( recover_cooldown > 0 )) && recover_cooldown=$(( recover_cooldown-1 ))
  if (( collapse_state == 0 && recover_active == 0 && recover_cooldown == 0 && tick > 600 )); then
    dirty=0
    for ((x=0;x<cols;x++)); do (( decay[x] > 25 )) && dirty=$(( dirty+1 )); done
    if (( dirty*100/cols > 35 )); then recover_active=1; recover_x=0; fi
  fi
  if (( recover_active )); then
    recover_x=$(( recover_x+2 ))
    if (( recover_x > cols+recover_width*3 )); then
      recover_active=0; recover_cooldown=300
    fi
    for ((hc=recover_x-recover_width*3; hc<=recover_x; hc++)); do
      if (( hc>=0 && hc<cols )); then
        decay[$hc]=$(( decay[hc]>3 ? decay[hc]-6 : 0 ))
        break_timer[$hc]=0
      fi
    done
  fi

  if (( collapse_state != 2 )); then
    cur_amp=$collapse_amp

    for ((x=0;x<cols;x++)); do
      phase=$(( (tick*7 + x*9) % 360 ))
      s=${SIN[$phase]}
      cosv=${COS[$phase]}

      y1=$(( center + (s*cur_amp/1000) ))
      y2=$(( center - (s*cur_amp/1000) ))
      drift=$(( SIN[$(((phase+90)%360))]*2/1000 ))
      y1=$(( y1+drift ))
      y2=$(( y2-drift ))

      if   (( cosv > 600 )); then depth=2
      elif (( cosv > -600 )); then depth=1
      else                         depth=0; fi

      if (( collapse_state == 0 )); then
        breathe=$(( SIN[$((tick * 2 % 360))] * (amp / 6) / 1000 ))
        y1=$(( y1 - breathe ))
        y2=$(( y2 + breathe ))
      fi

      if (( collapse_state == 1 )); then
        unzip=$(( (COLLAPSE_DISSOLVE - collapse_timer) * (amp / 2) / COLLAPSE_DISSOLVE ))
        y1=$(( y1 - unzip ))
        y2=$(( y2 + unzip ))
      fi

      if (( collapse_state == 0 && RANDOM%1000 < 8 )); then base_idx[x]=$((RANDOM%4)); fi

      if (( collapse_state == 0 )); then
        if (( post_reform_cooldown == 0 )); then
          if (( tick>100 && RANDOM%1000 < (1+tick/500) )); then
            decay[x]=$(( decay[x]+1 ))
          fi
          if (( decay[x] > 24 )); then
            if (( x > 0 && RANDOM%100 < 15 )); then
              decay[$((x-1))]=$(( decay[x-1] + 1 ))
            fi
            if (( x < cols-1 && RANDOM%100 < 15 )); then
              decay[$((x+1))]=$(( decay[x+1] + 1 ))
            fi
          fi
        fi
        if (( decay[x]>0 && RANDOM%1000 < 9 )); then
          decay[x]=$(( decay[x]-1 ))
        fi
      fi

      idx=${base_idx[x]}
      b=${bases[$idx]}
      c=${comp[$idx]}
      b_type=$depth
      c_type=$depth

      if (( glitch_timer > 0 )); then
        dist=$(( x-glitch_center ))
        (( dist<0 )) && dist=$(( -dist ))
        if (( dist < glitch_strength*5 )); then
          local_push=$(( (glitch_strength*5-dist)/2 ))
          jitter=$(( (RANDOM%3)-1 ))
          y1=$(( y1+jitter+local_push/3 ))
          y2=$(( y2-jitter-local_push/3 ))
          (( RANDOM%100<55 )) && decay[x]=$(( decay[x]+2 ))
        fi
      fi

      if (( decay[x]>10 && RANDOM%100 < decay[x] )); then
        b=${corrupt[$((RANDOM%${#corrupt[@]}))]}
        b_type=3
      fi
      if (( decay[x]>10 && RANDOM%100 < decay[x] )); then
        c=${corrupt[$((RANDOM%${#corrupt[@]}))]}
        c_type=3
      fi

      if (( collapse_state == 3 )); then
        melt=$(( reform_progress * 100 / COLLAPSE_REFORM ))
        if (( b_type != 3 )); then
          b_type=5
          if (( RANDOM % 100 >= melt )); then
            b=${crystal[$((RANDOM % ${#crystal[@]}))]}
          fi
        fi
        if (( c_type != 3 )); then
          c_type=5
          if (( RANDOM % 100 >= melt )); then
            c=${crystal[$((RANDOM % ${#crystal[@]}))]}
          fi
        fi
      fi

      if (( recover_active )); then
        trail=$(( recover_x - x ))
        if (( trail >= 0 && trail < recover_width )); then
          b_type=4; c_type=4
          (( trail < 3 )) && { b='+'; c='+'; }
        elif (( trail >= recover_width && trail < recover_width*3 )); then
          if (( trail < recover_width*2 )); then
            b_type=6; c_type=6
          else
            b_type=7; c_type=7
          fi
        fi
      fi

      if (( break_timer[x]>0 )); then break_timer[x]=$(( break_timer[x]-1 )); fi
      if (( collapse_state==0 && decay[x]>22 && break_timer[x]==0 && RANDOM%100<10 )); then
        break_timer[x]=$BREAK_TOTAL
      fi

      top=$y1; bot=$y2
      (( top>bot )) && { tmp=$top; top=$bot; bot=$tmp; }
      mid_y=$(( (top+bot)/2 ))
      bridge_height=$(( bot-top-1 ))

      if (( break_timer[x]==0 )); then
        rung_phase=$(( (x + tick/2) % 5 ))

        for ((y=top+1; y<bot; y++)); do
          dist_from_mid=$(( y-mid_y ))
          (( dist_from_mid < 0 )) && dist_from_mid=$(( -dist_from_mid ))

          if (( rung_phase == 0 )); then
            if (( dist_from_mid == 0 )); then
              ch='═'
              flag=$depth
            elif (( dist_from_mid == 1 && bridge_height > 3 )); then
              ch='│'
              flag=$depth
            else
              ch='·'
              flag=$depth
            fi
          else
            if (( dist_from_mid <= 1 )); then
              case $(((x+y+tick)%4)) in
                0) ch='·' ;; 1) ch=',' ;; 2) ch='`' ;; *) ch='.' ;;
              esac
              flag=$depth
            else
              if (( (x+y+tick)%7 == 0 )); then
                ch='·'
                flag=$depth
              else
                ch=' '
                flag=1
              fi
            fi
          fi

          if (( decay[x]>14 && RANDOM%100 < decay[x]/2 )); then
            ch=${corrupt[$((RANDOM%${#corrupt[@]}))]}
            flag=3
          fi

          if (( glitch_timer>0 )); then
            dist=$(( x-glitch_center ))
            (( dist<0 )) && dist=$(( -dist ))
            if (( dist<glitch_strength*4 && RANDOM%100<35 )); then
              ch=${corrupt[$((RANDOM%${#corrupt[@]}))]}
              flag=3
            fi
          fi

          if (( collapse_state == 3 )); then
            melt=$(( reform_progress * 100 / COLLAPSE_REFORM ))
            if (( RANDOM % 100 >= melt )); then
              ch=${crystal[$((RANDOM % ${#crystal[@]}))]}
              flag=5
            fi
          fi

          if (( recover_active )); then
            wave_dist=$(( x-recover_x ))
            (( wave_dist<0 )) && wave_dist=$(( -wave_dist ))
            if (( wave_dist<recover_width )); then
              flag=4
              (( wave_dist<3 )) && ch='|'
            elif (( wave_dist >= recover_width && wave_dist < recover_width*3 )); then
              if (( wave_dist < recover_width*2 )); then
                flag=6
              else
                flag=7
              fi
            fi
          fi

          put_char "$y" "$x" "$ch" $flag
        done

        if (( depth >= 1 )); then
          if (( y1-1 >= 0 )); then
            put_char $((y1-1)) "$x" '·' $((depth>0 ? depth-1 : 0))
          fi
          if (( y2+1 < rows )); then
            put_char $((y2+1)) "$x" '·' $((depth>0 ? depth-1 : 0))
          fi
        fi

        if (( depth == 2 )); then
          if (( y1-2 >= 0 )); then
            put_char $((y1-2)) "$x" ',' $((depth>1 ? depth-2 : 0))
          fi
          if (( y2+2 < rows )); then
            put_char $((y2+2)) "$x" ',' $((depth>1 ? depth-2 : 0))
          fi
        fi

      elif (( break_timer[x] > BREAK_TOTAL-BREAK_SNAP )); then
        snap_frame=$(( BREAK_TOTAL-break_timer[x] ))
        tear_radius=$(( snap_frame*bridge_height/(BREAK_SNAP*2)+1 ))
        for ((y=top+1; y<bot; y++)); do
          dist_from_mid=$(( y-mid_y ))
          (( dist_from_mid<0 )) && dist_from_mid=$(( -dist_from_mid ))
          if (( dist_from_mid<=tear_radius )); then
            if (( dist_from_mid==tear_radius )); then
              case $(( (x+y+tick)%4 )) in 0) ch='\';; 1) ch='/';; 2) ch='~';; *) ch='-';; esac
              put_char "$y" "$x" "$ch" 3
            fi
          else
            ch=${rungs[$(((x+y+tick)%${#rungs[@]}))]}
            put_char "$y" "$x" "$ch" $depth
          fi
        done
        case $(( tick%3 )) in 0) b_type=3;b='\';; 1) b_type=3;b='/';; *) b_type=3;; esac
        case $(( (tick+1)%3 )) in 0) c_type=3;c='\';; 1) c_type=3;c='/';; *) c_type=3;; esac
      else
        fall_age=$(( BREAK_TOTAL - break_timer[x] - BREAK_SNAP ))
        max_fall=$(( cols / 4 ))

        if (( fall_age < max_fall )); then
          dy1=$(( y1 + fall_age + ( (x+tick)%2 ) ))
          if (( dy1 >= 0 && dy1 < rows )); then
            ch=${shards[$(( (x+tick) % 5 ))]}
            ctype_fall=$(( depth > 0 ? depth - 1 : 0 ))
            put_char "$dy1" "$x" "$ch" $ctype_fall
          fi

          dy2=$(( y2 + fall_age + ( (x+tick+1)%2 ) ))
          if (( dy2 >= 0 && dy2 < rows )); then
            ch=${shards[$(( (x+tick+2) % 5 ))]}
            put_char "$dy2" "$x" "$ch" $ctype_fall
          fi

          dy3=$(( mid_y + fall_age + ( (x+tick)%3 ) - 1 ))
          if (( dy3 >= 0 && dy3 < rows )); then
            ch=${shards[$(( (x+tick+3) % 5 ))]}
            put_char "$dy3" "$x" "$ch" 3
          fi
        fi

        for ((y=top; y<=bot; y++)); do
          put_char "$y" "$x" " " 1
        done

        case $(( tick%4 )) in 0) b_type=3;b='!';; 1) b_type=3;b='|';; *) ;; esac
        case $(( (tick+2)%4 )) in 0) c_type=3;c='!';; 1) c_type=3;c='|';; *) ;; esac
      fi

      put_char "$y1" "$x" "$b" $b_type
      put_char "$y2" "$x" "$c" $c_type

      if (( collapse_state==0 && decay[x]>18 && RANDOM%100<10 )); then
        put_char "$(( (y1+y2)/2 ))" "$x" '¦' 3
      fi
    done

    if (( glitch_timer>0 )); then
      pulse_y=$(( center+(SIN[$((tick*18%360))]*(amp/3)/1000) ))
      for ((dx=-glitch_strength*3; dx<=glitch_strength*3; dx++)); do
        x=$((glitch_center+dx)); (( x<0||x>=cols )) && continue
        case $(((dx<0?-dx:dx)%5)) in 0)ch='*';;1)ch='+';;2)ch='=';;3)ch='-';;*)ch=':';;esac
        (( RANDOM%100<45 )) && ch=${corrupt[$((RANDOM%${#corrupt[@]}))]}
        put_char "$pulse_y"       "$x" "$ch" 3
        put_char $((pulse_y-1)) "$x" '.' 3
        put_char $((pulse_y+1)) "$x" '.' 3
      done
      glitch_timer=$(( glitch_timer-1 ))
    fi

  fi

  if (( post_reform_cooldown > 0 )); then
    post_reform_cooldown=$(( post_reform_cooldown - 1 ))
  fi

  half=$(( PULSE_STEPS/2 ))
  frames_elapsed=$(( PULSE_STEPS-glitch_pulse ))
  if (( frames_elapsed<half )); then
    pulse_idx=$(( frames_elapsed*(PULSE_STEPS-1)/half ))
  else
    pulse_idx=$(( (PULSE_STEPS-1)-(frames_elapsed-half)*(PULSE_STEPS-1)/half ))
  fi
  (( pulse_idx<0 )) && pulse_idx=0
  (( pulse_idx>=PULSE_STEPS )) && pulse_idx=$(( PULSE_STEPS-1 ))

  out=''
  for ((i=0;i<rows;i++)); do
    out+=$'\033'; out+="[$((i+1));1H"
    cur_color=''
    for ((j=0;j<cols;j++)); do
      cidx=$(( i*cols+j ))
      ch="${chars[$cidx]}"
      ctype="${cell_type[$cidx]}"

      if [[ "$ch" == ' ' ]]; then
        [[ -n "$cur_color" ]] && { out+="$COL_RESET"; cur_color=''; }
        out+=' '
      else
        if   (( ctype==3 )); then want='corrupt'
        elif (( ctype==4 )); then want='recover'
        elif (( ctype==6 )); then want='rmid'
        elif (( ctype==7 )); then want='rdim'
        elif (( ctype==5 )); then want='reform'
        elif (( glitch_pulse>0 )); then
          case $ctype in 0)want='pd';;1)want='pm';;*)want='pb';; esac
        else
          case $ctype in 0)want='dim';;1)want='mid';;*)want='bright';; esac
        fi

        if [[ "$cur_color" != "$want" ]]; then
          case $want in
            dim)     out+="$COL_DNA_DIM"              ;;
            mid)     out+="$COL_DNA_MID"              ;;
            bright)  out+="$COL_DNA_BRIGHT"           ;;
            corrupt) out+="$COL_CORRUPT"              ;;
            recover) out+="$COL_RECOVER"              ;;
            rmid)    out+="$COL_RECOVER_MID"          ;;
            rdim)    out+="$COL_RECOVER_DIM"          ;;
            reform)  out+="$COL_REFORM"               ;;
            pb)      out+="${PULSE_BRIGHT[$pulse_idx]}";;
            pm)      out+="${PULSE_MID[$pulse_idx]}"   ;;
            pd)      out+="${PULSE_DIM[$pulse_idx]}"   ;;
          esac
          cur_color="$want"
        fi
        out+="$ch"
      fi
    done
    out+="$COL_RESET"
  done

  crit_cols=0
  for ((x=0;x<cols;x++)); do (( decay[x] >= 27 )) && crit_cols=$(( crit_cols+1 )); done
  decay_pct=$(( (crit_cols * 10000) / (cols * 85) ))
  (( decay_pct > 100 )) && decay_pct=100

  if   (( collapse_state==1 )); then wave_str="COLLAPSE"
  elif (( collapse_state==2 )); then wave_str="CHAOS   "
  elif (( collapse_state==3 )); then wave_str="REFORM  "
  elif (( recover_active   )); then
    wave_pct=$(( recover_x * 100 / cols ))
    (( wave_pct > 100 )) && wave_pct=100
    wave_str="REPAIR ${wave_pct}%"
  elif (( post_reform_cooldown > 0 )); then
    cd_sec=$(( (post_reform_cooldown + FPS - 1) / FPS ))
    wave_str="STABLE ${cd_sec}s"
  elif (( recover_cooldown>0 )); then wave_str="COOLDOWN"
  else                               wave_str="STANDBY "; fi

  bar_fill=$(( decay_pct/10 )); decay_bar=''
  for ((b=0;b<10;b++)); do (( b<bar_fill )) && decay_bar+='█' || decay_bar+='░'; done

  if [[ "$SHOW_STATS" == "true" ]]; then
  ov_lines=(
    "┌──────────────────────┐"
    "│ FPS   : ${FPS}           │"
    "│ DECAY : [${decay_bar}] ${decay_pct}% │"
    "│ STATE : ${wave_str}     │"
    "│ R = manual recovery  │"
    "└──────────────────────┘"
  )
  ov_width=24; ov_col=$(( cols-ov_width )); (( ov_col<1 )) && ov_col=1
  for ((li=0;li<${#ov_lines[@]};li++)); do
    line="${ov_lines[$li]:0:$ov_width}"
    while (( ${#line}<ov_width )); do line+=' '; done
    out+=$'\033'; out+="[$((li+2));${ov_col}H"
    out+="${COL_DNA_DIM}${line}${COL_RESET}"
  done
  fi

  printf '%s' "$out"
  tick=$(( tick+1 ))
  sleep "$FRAME_SLEEP"
done
