<?php
/**
 * My Smarty Compiler Class
 *
 * This is an derivative of the standard PHP Smarty Compiler class. The class
 * has been extended to allow cached attributes on plugins to optionally work
 * the same as 'insert' tags and cache all given arguments and not just those
 * named when the plugin was registered.
 *
 * @author  Nick Turner
 * @version 1.1
 */

require_once(SMARTY_DIR.'Smarty_Compiler.class.php');

class MySmarty_Compiler extends Smarty_Compiler {

    /*
    function _parse_var($var_expr)
    {
        EBET_Profile('MySmarty_Compiler::_parse_var(', $var_expr, ')');

        static $plugin_var_names_regex = null;
        if (!isset($plugin_var_names_regex) && isset($this->_plugins['variable'])) {
            $cb = create_function('$s', 'return preg_quote($s, \'~\');');
            $plugin_var_names_regex = '~\$('.join('|', array_map($cb, array_keys($this->_plugins['variable']))).')~';
        }

        if ($plugin_var_names_regex) {
            if (preg_match($plugin_var_names_regex, $var_expr, $matches)) {
                EBET_Profile('MySmarty_Compiler::_parse_var() - matched plugin variable name ', $matches);
            }
        }

        return parent::_parse_var($var_expr);
    }
     */

    /**
     * compile argument list
     *
     * We use this to handle the special cases we use for the list of cachable
     * attributes.
     *
     * At present setting this list to the integer 1 will mean the list is
     * dynamic and includes all parameters passed during that invocation of
     * the plugin. Setting it to integer 0 means cache no attributes.
     *
     * @access  private
     * @param   string  $type           type of plugin
     * @param   string  $name           name of plugin
     * @param   string  $attrs          list of attrs to pass to plugin
     * @param   string  $cache_code     PHP output for cache 
     * @return  string                  PHP argument list for plugin
     */
    function _compile_arg_list($type, $name, $attrs, &$cache_code) {
        if (isset($type) && isset($name) &&
            isset($this->_plugins[$type]) &&
            isset($this->_plugins[$type][$name]) &&
            empty($this->_plugins[$type][$name][4]) &&
            is_int($this->_plugins[$type][$name][5])) {
            $cache_attrs = $this->_plugins[$type][$name][5];
            switch ($cache_attrs) {
                case 1:
                    /* Cache all supplied attributes */
                    $this->_plugins[$type][$name][5] = array_keys($attrs);
                    $ret = parent::_compile_arg_list($type, $name, $attrs, $cache_code);
                    break;
                default:
                    /* Cache no attributes */
                    $this->_plugins[$type][$name][5] = null;
                    $ret = parent::_compile_arg_list($type, $name, $attrs, $cache_code);
                    break;
            }
            $this->_plugins[$type][$name][5] = $cache_attrs;
            return $ret;
        } else {
            return parent::_compile_arg_list($type, $name, $attrs, $cache_code);
        }
    }


    /**
     * Constructor
     *
     * @access  public
     */
    function MySmarty_Compiler() {

        Smarty_Compiler::Smarty_Compiler();

    }

    /**
     * Destructor
     *
     * @access  private
     */
    function _MySmarty_Compiler() {

    }

}

// The extract sub cached sections from main template
function _mysmarty_prefilter_cache($source, &$smarty) {

    // Extract the {cache}...{/cache} sections from the file. The sections
    // can be nested so we only extract the outermost level. Sections are
    // not extracted from {php} or {literal} sections.

    $ld = $smarty->left_delimiter;
    $rd = $smarty->right_delimiter;

    $ldq = preg_quote($ld, '~');
    $rdq = preg_quote($rd, '~');

    $tag = preg_quote(MYSMARTY_TAG_CACHE, '~');

    $ncv_pfx = preg_quote(MYSMARTY_PFX_NOCACHE_VAR, '~');
    $ncv_tag = $ncv_pfx . '(' . $smarty->_obj_call_regexp . '|' . $smarty->_var_regexp . ')';

    // Bail out if no subcached blocks used in template
    if (!preg_match("~{$ldq}\s*{$ncv_tag}|/?{$tag}.*?{$rdq}~si", $source)) {
        //trace('no cache tags in source: '.$smarty->_current_file);
        return $source;
    }

    // fetch all special blocks
    $re = "~{$ldq}\*.*?\*{$rdq}|{$ldq}\s*literal\s*{$rdq}.*?{$ldq}\s*/literal\s*{$rdq}|{$ldq}\s*php\s*{$rdq}.*?{$ldq}\s*/php\s*{$rdq}~s";

    $literals = null;
    if (preg_match_all($re, $source, $match)) {
        $literals = $match[0];

        // replace all literals with a place holder
        $literal_count = 0;
        $source = preg_replace($re.'e', "'%%%SMARTY_LITERAL'.(\$literal_count++).'%%%'", $source);
    }

    // replace all nvc blocks
    $source = preg_replace("~({$ldq}\s*){$ncv_tag}(.*?{$rdq})~si", '$1'.MYSMARTY_TAG_NOCACHE_VAR.' value=$2$3', $source);

    // fetch all cache tag blocks
    if (preg_match_all("~{$ldq}(/?){$tag}(.*?){$rdq}~si", $source, $match, PREG_PATTERN_ORDER)) {
        // remove everything between every pair of outermost tags and replace
        // it with a single template include tag

        $match_count = count($match[0]);
        $results = preg_split("~({$ldq}/?(?:{$tag}).*?{$rdq})~si", $source, -1, PREG_SPLIT_DELIM_CAPTURE);

        $path = $smarty->_get_auto_filename($smarty->compile_dir, $smarty->_current_file, $smarty->_compile_id);
        if (!preg_match('/^([\/\\\\]|[a-zA-Z]:[\/\\\\])/', $path)) {
            // make it an absolute path so resource is read ok
            $path = getcwd() . DIRECTORY_SEPARATOR . $path;
        }

        $fetch_tag = MYSMARTY_TAG_FETCH;

        $level = 0;
        $j = 0; $max_j = count($match[0]);

        $content = array();
        $content_params = '';
        $content_count = 0;

        // Process the found start points in reverse order
        for ($i = 0, $max_i = count($results); $i < $max_i && $j < $max_j; $i++) {
            if ($results[$i] == $match[0][$j]) {
                // cache tag
                if ($match[1][$j] == '/') {
                    // closing tag
                    $level--;

                    if ($level == 0) {
                        // blank lines are copied to source (to preserve line numbering)
                        $results[$i] = str_repeat("\n", substr_count($results[$i], "\n"));

                        // prepare the content
                        $filename = "{$path}.{$content_count}";
                        $contents = implode('', $content);
                        if ($literals) {
                            $contents = preg_replace('/%%%SMARTY_LITERAL(\d+)%%%/e', '$literals[\1]', $contents);
                        }

                        // write the block content to a separate template file
                        $_params = array(
                            'filename' => $filename,
                            'contents' => $contents,
                            'create_dirs' => true);

                        require_once(SMARTY_CORE_DIR . 'core.write_file.php');
                        smarty_core_write_file($_params, $smarty);

                    } else {
                        // source is copied to content
                        $content[] = $results[$i];

                        // blank lines are copied to source (to preserve line numbering)
                        $results[$i] = str_repeat("\n", substr_count($results[$i], "\n"));
                    }
                } else {
                    // opening tag

                    if ($level == 0) {
                        $content = array();
                        $content_params = $match[2][$j];
                        $content_count++;

                        $filename = "{$path}.{$content_count}";

                        // {include_tpl ...} tag is copied to source
                        $results[$i] = "${ld}{$fetch_tag}{$content_params} _file='file:{$filename}'{$rd}";
                    } else {
                        // source is copied to content
                        $content[] = $results[$i];

                        // blank lines are copied to source (to preserve line numbering)
                        $results[$i] = str_repeat("\n", substr_count($results[$i], "\n"));
                    }

                    $level++;
                }
                $j++;
            } else if ($level > 0) {
                // source is copied to content
                $content[] = $results[$i];

                // blank lines are copied to source (to preserve line numbering)
                $results[$i] = str_repeat("\n", substr_count($results[$i], "\n"));
            }
        }

        $source = implode('', $results);
    }

    $source = preg_replace('/%%%SMARTY_LITERAL(\d+)%%%/e', '$literals[\1]', $source);

    return $source;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */

?>
