<?php
class LightnCandy {
    public static $flagErrorlog = false;
    public static $flagThrowException = false;
    public static $flagStandalone = false;
    private static $lastContext;
    const TOKEN_SEARCH = '/(\\{{2,3})(.+?)(\\}{2,3})/';

    public static function compile($template) {
        $context = Array(
            'level' => 0,
            'stack' => Array(),
            'error' => false,
            'useVar' => false,
            'obj' => self::$flagStandalone ? '$' : 'LightnCandy::',
            'usedFeature' => Array(
                'enc' => 0,
                'raw' => 0,
                'sec' => 0,
                'isec' => 0,
                'if' => 0,
                'else' => 0,
                'each' => 0,
            )
        );

        if (preg_match_all(self::TOKEN_SEARCH, $template, $tokens, PREG_SET_ORDER) > 0) {
            foreach ($tokens as $token) {
                self::scan($token, $context);
            }
        }
        self::$lastContext = $context;

        if ($context['error']) {
            if (self::$flagErrorlog) {
                error_log($context['error']);
            }
            if (self::$flagThrowException) {
                throw new Exception($context['error']);
            }
            return false;
        }

        if ($context['usedFeature']['sec'] < 1) {
            $context['useVar'] = Array('$in');
        }

        $code = preg_replace_callback(self::TOKEN_SEARCH, function ($matches) use (&$context) {
            return '\'' . LightnCandy::tokens($matches, $context) . '\'';
        }, $template);

        return "<?php return function (\$in) {
    \$cx = Array(
        'path' => Array(),
        'parents' => Array()
    );
    return '$code';
}
?>";
    }

    public static function getContext() {
        return self::$lastContext;
    }

    protected static function scan($token, &$context) {
        $head = substr($token[2], 0, 1);
        $act = substr($token[2], 1);
        $raw = ($token[1] === '{{{');

        if (count($token[1]) !== count($token[3])) {
            $context['error'] = "Bad token {$token[1]}{$token[2]}{$token[3]} ! Do you mean {{}} or {{{}}}?";
            return;
        }

        if ($raw) {
            if (preg_match('/\\^|\\/|#/', $head)) {
                $context['error'] = "Bad token {$token[1]}{$token[2]}{$token[3]} ! Do you mean \{\{{$token[2]}\}\}?";
                return;
            }
        }
        switch ($head) {
        case '^':
            return $context['usedFeature']['isec'] ++;
        case '#':
            switch ($act) {
            case 'if':
                return $context['usedFeature']['if'] ++;
            case 'else':
                return $context['usedFeature']['else'] ++;
            case 'each':
                return $context['usedFeature']['each'] ++;
            default:
                return $context['usedFeature']['sec'] ++;
            }
        default:
            $fn = $raw ? 'raw' : 'enc';
            return $context['usedFeature'][$fn] ++;
        }
    }

    public static function tokens($token, &$context) {
        $head = substr($token[2], 0, 1);
        $act = substr($token[2], 1);
        $raw = ($token[1] === '{{{');

        switch ($head) {
        case '^':
            $context['stack'][] = $act;
            $context['stack'][] = '^';
            if ($context['useVar']) {
                $v = end($context['useVar']) . "['{$act}']";
                return ".((is_null($v) && ($v !== false)) ? ("; 
            } else {
                return ".({$context['obj']}isec('$act', \$in) ? (";
            }
        case '/':
            switch ($act) {
            case 'if':
                $pop = array_pop($context['stack']);
                if ($pop == ':') {
                    $pop = array_pop($context['stack']);
                    return ')).';
                }
                return ') : \'\').';
            case 'each':
                $act = substr($act, 5);
            default:
                $context['level']--;
                $pop = array_pop($context['stack']);
                switch($pop) {
                case '#':
                case '^':
                    $pop2 = array_pop($context['stack']);
                    if ($pop2 !== $act) {
                        $context['error'] = "Unexpect token {$token[2]} ! Previous token $pop$pop2 is not closed";
                        return;
                    }
                    if ($pop == '^') {
                        return ") : '').";
                    }
                    return ";}).";
                default:
                    $context['error'] = "Unexpect token: {$token[2]} !";
                    return;
                }
            }
        case '#':
            switch ($act) {
            case 'if':
                $act = substr($act, 3);
                return ".({$context['obj']}ifvar('{$act}', \$in) ? (";
            case 'else':
                $context['stack'][] = ':';
                return ') : (';
            case 'each':
                $act = substr($act, 5);
            default:
                $each = ($act == 'each') ? 'true' : 'false';
                $context['level']++;
                $context['stack'][] = $act;
                $context['stack'][] = '#';
                return ".{$context['obj']}sec('$act', \$cx, \$in, $each, function(\$cx, \$in) {return ";
            }
        default:
            $fn = $raw ? 'raw' : 'enc';
            if ($context['useVar']) {
                $v = end($context['useVar']) . "['{$token[2]}']";
                return $raw ? ".(($v === true) ? 'true' : $v)." : ".(($v === true) ? 'true' : htmlentities($v, ENT_QUOTES)).";
            } else {
                return ".{$context['obj']}{$fn}('{$token[2]}', \$cx, \$in).";
            }
        }
    }

    public static function ifvar($var, $in) {
        return (!is_null($var) && ($var !== false));
    }

    public static function raw($var, $cx, $in) {
        return ($in[$var] === true) ? 'true' : $in[$var];
    }

    public static function enc($var, $cx, $in) {
        return htmlentities(($in[$var] === true) ? 'true' : $in[$var], ENT_QUOTES);
    }

    public static function sec($var, &$cx, $in, $each, $cb) {
        if (!$each && is_array($in[$var])) {
            if (count(array_diff_key($var, array_keys(array_keys($in[$var])))) == 0) {
                $each = true;
            }
        }
        if ($each) {
            $ret = Array();
            foreach ($in[$var] as $raw) {
                $ret[] = $cb($cx, $raw);
            }
            return join('', $ret);
        }
        if (is_array($in[$var])) {
            return $cb($cx, $in[$var]);
        }
        if (!is_null($in[$var]) && ($in[$var] !== false)) {
            return $cb($cx, $in[$var]);
        }
        return '';
    }

    public static function isec($var, $in) {
        return !self::ifvar($var, $in);
    }

    public static function prepare($php) {
        return include('data://text/plain,' . urlencode($php));
    }

    public static function render($prepared, $data) {
        $func = include($prepared);
        return $func($data);
    }
}
?>