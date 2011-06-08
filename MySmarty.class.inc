<?php
/**
 * My Smarty Class
 *
 * This is an derivative of the standard PHP Smarty template class. The class
 * has been extended to allow cached and non-cached blocks and support for
 * last_modified and expiry times allowing better support for handling of
 * any 'If_Modified_Since' style HTTP headers with a '304 Not Modified' style
 * response.
 *
 * @author  Nick Turner
 * @version 1.1
 */

if (!defined('SMARTY_DIR'))
    define('SMARTY_DIR', dirname(dirname(__FILE__)).'/Smarty-2.6.19/libs/');

if (!defined('MYSMARTY_DIR'))
    define('MYSMARTY_DIR', dirname(__FILE__).DIRECTORY_SEPARATOR);

if (!defined('MYSMARTY_TAG_CACHE'))
    define('MYSMARTY_TAG_CACHE', 'cache');

if (!defined('MYSMARTY_TAG_NOCACHE'))
    define('MYSMARTY_TAG_NOCACHE', 'nocache');

if (!defined('MYSMARTY_TAG_NOCACHE_VAR'))
    define('MYSMARTY_TAG_NOCACHE_VAR', 'nocache_var');

if (!defined('MYSMARTY_PFX_NOCACHE_VAR'))
    define('MYSMARTY_PFX_NOCACHE_VAR', '+');

if (!defined('MYSMARTY_TAG_FETCH'))
    define('MYSMARTY_TAG_FETCH', 'fetch');

require_once(SMARTY_DIR.'Smarty.class.php');

/**
 * This object is used to define and display web site pages
 */
class MySmarty extends Smarty {

    var $force_regenerate = false;      // Force non cached output

    var $cache_id = null;               // Default Cache id
    var $cache_must_validate = false;   // Client must validate cache on every request
    var $cache_directive = 'public, must-revalidate';    // Client cache directive

    var $_modify_time = null;           // Time fetched output was modified
    var $_expiry_time = null;           // Time fetched output expires

    /**
     * called for included templates
     *
     * Same as default Smarty method with the exception;
     *
     * The parameters passed to the fetch call are available in the template
     * variable $params from within the fetched template.
     *
     * Any 'file' or 'assign' parameter is not available in $params.
     *
     * @access  private
     * @param   array   $params     parameters
     * @return  string              fetched template output
     */
    function _smarty_include($params)
    {
        $params['smarty_include_vars']['params'] = $params['smarty_include_vars'];
        return parent::_smarty_include($params);
    }

    /**
     * Called to fetch sub templates
     *
     * The parameters passed to the fetch call are available in the template
     * variable $params from within the fetched template.
     *
     * Any 'file' or 'assign' parameter is not available in $params.
     *
     * @access  private
     * @param   array   $params     parameters
     * @return  string              fetched template output
     */
    function _mysmarty_fetch($params) {

        // Backup the current class variables
        $_this = (array)$this;

        if (isset($params['name'])) {
            // Call this given plugin func with any arguments (it can set up any caching values)
            $_funcname = $this->_plugins['fetch'][$params['name']][0];
            $_funcname($params['args'], $params['file'], $this);
        } else if (!empty($params['args'])) {
            // Pull out our reserved params - then put the rest in the template vars
            foreach ($params['args'] as $k => $v) { 
                switch ($k) {
                    case 'cache_enable':
                        $this->caching = $v;
                        break;
                    case 'cache_id':
                        $this->cache_id = $v;
                        break;
                    case 'cache_lifetime':
                        $this->cache_lifetime = $v;
                        break;
                    case 'cache_compile_id':
                        $this->compile_id = $v;
                        break;
                    default:
                        $this->assign($k, $v); 
                        break;
                 }
            }
        }

        $this->assign('params', $params['args']);

        $output = $this->fetch($params['file'], $this->cache_id, $this->compile_id, false);

        $modify_time = $this->_modify_time;
        $expiry_time = $this->_expiry_time;

        // Restore the saved class variables
        foreach ($_this as $member => $value) {
            if ($member != '_smarty_debug_info') {
                $this->$member = $value;
            }
        }

        $this->modify_time($modify_time);
        $this->expiry_time($expiry_time);

        return $output;
    }

    /**
     * Callback function for preg_replace, to call a non-cacheable block
     *
     * We override this to handle the last modified time call back. We also
     * ensure the $this->_cache_serial and $this->_nocache_count values are
     * set to the same values as they would be during compilation.
     *
     * @access  public
     * @param   array   $match      preg_replace matches
     * @return  string              plugin output
     */
    function _process_compiled_include_callback($match) {

        // additional start
        $this->_has_set_modify_time = false;
        $this->_has_set_expiry_time = false;
        $this->_cache_serial = $match[2];
        $this->_nocache_count = $match[3];
        // additional end

        $_func = '_smarty_tplfunc_'.$match[2].'_'.$match[3];
        ob_start();
        $_func($this);
        $_ret = ob_get_contents();
        ob_end_clean();

        // additional start
        if (!$this->_has_set_modify_time) {
            $this->modify_time(time());
        }
        if (!$this->_has_set_expiry_time) {
            $this->expiry_time(0);
        }
        // additional end

        return $_ret;
    }

    /**
     * Registers fetch function to be used in templates
     *
     * An 'fetch' function is called when the 'name' parameter
     * is passed to the 'fetch' template tag. It is passed the
     * tag parameters, the name of the template to be fetched
     * and a reference to the Smarty object. It can configure the
     * Smarty object ready for fetching the named template,
     * setting any caching and compile parameters as required.
     *
     * @access  public
     * @param   string      $name       name of fetch function
     * @param   callback    $func       function to call
     * @return  void
     */
    function register_fetch_function($name, $func) {
        $this->_plugins['fetch'][$name] =
            array($func, null, null, false, false, null);
    }

    /**
     * Unregisters fetch function
     *
     * @access  public
     * @param   string      $name       name of fetch function
     * @return  void
     */
    function unregister_fetch_function($name) {
        unset($this->_plugins['fetch'][$name]);
    }

    /**
     * Registers insert function to be used in templates
     *
     * An 'insert' function is called when the 'name' parameter
     * is passed to the 'insert' template tag. It is passed the
     * tag parameters, and a reference to the Smarty object.
     *
     * @access  public
     * @param   string      $name       name of insert function
     * @param   callback    $func       function to call
     * @return  void
     */
    function register_insert_function($name, $func) {
        $this->_plugins['insert'][$name] =
            array($func, null, null, false, false, null);
    }

    /**
     * Unregisters insert function
     *
     * @access  public
     * @param   string      $name       name of insert function
     * @return  void
     */
    function unregister_insert_function($name) {
        unset($this->_plugins['insert'][$name]);
    }

    /**
     * Registers variable to be used in templates
     *
     * An 'variable' function is called when the $name syntax is
     * used and the name is registered here. It is passed the
     * variable name and indices as an array.
     *
     * It allows the user to define special variables who's values
     * can be calculated at runtime time similar to the reserved
     * $smarty variable.
     *
     * @access  public
     * @param   string      $name       name of variable
     * @param   callback    $func       function to call
     * @return  void
     */
    /*
    function register_variable($name, $func) {
        $this->_plugins['variable'][$name] =
            array($func, null, null, false, false, null);
    }
    */

    /**
     * Unregisters fetch function
     *
     * @access  public
     * @param   string      $name       name of fetch function
     * @return  void
     */
    /*
    function unregister_variable($name) {
        unset($this->_plugins['variable'][$name]);
    }
     */

    /**
     * Get/Set the modified timestamp
     *
     * Insert plugins and none cached plugins can call this to indicate the
     * time the data they are inserting was last modified. If they do not
     * call this then for backwards compatibilty it is assumed that the data
     * they are including is new as if they called this with the current
     * time.
     *
     * Pass $time as any non null value to modify the current value. The
     * current value will only be altered if the new value is greater.
     *
     * @access  public
     * @param   integer     $time   time the data was last modified
     * @return  integer             current modification time
     */
    function modify_time($time = null) {
        if (!is_null($time)) {
            if ($time > $this->_modify_time) {
                $this->_modify_time = $time;
            }
            $this->_has_set_modify_time = true;
        }
        return $this->_modify_time;
    }

    /**
     * Get/Set the expires timestamp
     *
     * Insert plugins and none cached plugins can call this to indicate the
     * time the data they are inserting is set to expire. If they do not
     * call this then for backwards compatibilty it is assumed that the data
     * they are including is dynamic and expires immediately.
     *
     * Pass $time as any non null value to modify the current value. The
     * current value will only be altered if the new value is less.
     *
     * Note: 0 is expires immediate and -1 is never expires, thus -1 is an
     * infinately large number and greater than any other value.
     *
     * @access  public
     * @param   integer     $time   time the data was last modified
     * @return  integer             current modification time
     */
    function expiry_time($time = null) {
        if (!is_null($time)) {
            if (is_null($this->_expiry_time) || ($time != -1 && $time < $this->_expiry_time)) {
                $this->_expiry_time = $time;
            }
            $this->_has_set_expiry_time = true;
        }
        return $this->_expiry_time;
    }

    /**
     * Return the given timestamp in HTTP date format (RFC 1123 date format)
     *
     * @access  public
     * @param   integer     $time   timestamp to format
     * @return  string              formatted timestamp
     */
    function http_date($time) {
        return gmdate('D, d M Y H:i:s', $time).' GMT';
    }

    /**
     * Check any 'If-Modified-Since' header.
     *
     * This will check the current modification time of the last output
     * obtained from the {@link fetch()} method against the given $time
     * parameter. If no $time parameter is provided then any time given
     * by a 'If-Modified-Since' HTTP header in the current request is
     * used.
     *
     * If the output has not been modified then false will be returned, else
     * true will be returned.
     *
     * If $time is not specified and no 'If-Modified-Since' HTTP header is
     * available then the method returns true indicating the data was
     * modified.
     *
     * In keeping with HTTP convention a data is deemed to be modified if the
     * last modification time does not exactly match the 'If-Modified-Since'
     * time and not if it is just less than. For best results the server needs
     * to send the 'Last-Modified' HTTP header for the client to return the
     * correct 'If-Modified-Since' HTTP header.
     *
     * @access  public
     * @param   integer     $time   a time to use instead of HTTP header
     * @return  boolean             true if modified since given date
     */
    function if_modified_since($time = null) {
        if (is_null($this->_modify_time)) {
            $this->trigger_error('no modified time - please call fetch() first');
            return true;
        }

        if (!$time) {
            // See if we have a 'If-Modified-Since' header
            $_server_vars = ($this->request_use_auto_globals) ? $_SERVER : $GLOBALS['HTTP_SERVER_VARS'];
            $if_modified_since_date = @substr($_server_vars['HTTP_IF_MODIFIED_SINCE'], 0, strpos($_server_vars['HTTP_IF_MODIFIED_SINCE'], 'GMT') + 3);
        } else {
            $if_modified_since_date = $this->http_date($time);
        }

        $last_modified_date = $this->http_date($this->modify_time());

        return ($if_modified_since_date != $last_modified_date);
    }

    /**
     * Send any HTTP headers appropriate.
     *
     * This will send the correct HTTP headers for the last output
     * obtained from the {@link fetch()} method.
     *
     * The headers will include;
     *   + 'Status'         - the status '200' or '304'
     *   + 'Date'           - the date the output was generated
     *   + 'Last-Modified'  - the date the output was last modified
     *   + 'Expires'        - the date the output is set to expire
     *   + 'Cache-Control'  - the correct cache control output
     *
     * For data that should always be first validated by the client
     * on every request the $must_validate parameter can be set. If
     * not provided the $this->cache_must_validate value is used which
     * defaults to false. When $must_validate is true the 'Expires'
     * header and 'Cache-Control' headers will be set to indicate the
     * data has expired - even if the actual value returned by
     * {@link expiry_time()} indicates it hasn't. This will cause the
     * client to request the page with an 'If-Modified-Since' header
     * each time it is required and thus allow the server to see if new
     * data needs sending.
     *
     * The cache control directive can be set using the $directive. If
     * not provided the $this->cache_directive value is used which
     * defaults to 'public, must-revalidate' is used.
     *
     * NB: To effectively use this method it has to be called after a
     * call to {@link fetch()} and thus the fetch can not output any
     * data unless PHP output buffering is enabled. Otherwise the headers
     * can not be sent after the data is output.
     *
     * A '304 Not Modified' header can be sent if the if_modified_since()
     * method returns false. Otherwise a '200 OK' header is sent.
     *
     * @access  public
     * @param   boolean $must_validate  client must always validate requests
     * @param   boolean $directive      client caching directive
     * @return  boolean                 true if headers were sent
     */
    function http_headers($must_validate = null, $directive = null) {
        if (headers_sent()) {
            return false;
        }

        if (!isset($must_validate)) {
            $must_validate = $this->cache_must_validate;
        }

        if (!isset($directive)) {
            $directive = $this->cache_directive;
        }

        if (is_null($this->_modify_time)) {
            $this->trigger_error('no modified time - please call fetch() first');
            return false;
        }

        $current_time = time();
        $modify_time = $this->modify_time();
        $expiry_time = $this->expiry_time();

        if ($must_validate) {
            // Expires immediately
            $expiry_time = $current_time;
            $max_age = 0;
        } else if ($expiry_time < 0) {
            // Never expires
            $max_age = (365 * 24 * 60 * 60); // approx. 1 year in the future
            $expiry_time = $current_time + $max_age;
        } else if ($expiry_time <= $current_time) {
            // Expires immediately
            $expiry_time = $current_time;
            $max_age = 0;
        } else {
            // Actual expires time
            $max_age = ($expiry_time - $current_time);
        }

        if (!$this->if_modified_since()) {
            if (php_sapi_name() == 'cgi')
                header('Status: 304 Not Modified');
            else
                header('HTTP/1.1 304 Not Modified');
        } else {
            if (php_sapi_name() == 'cgi')
                header('Status: 200 OK');
            else
                header('HTTP/1.1 200 OK');
        }

        header('Date: '.$this->http_date($current_time));
        header('Last-Modified: '.$this->http_date($modify_time));
        header('Expires: '.$this->http_date($expiry_time));
        if (strstr($directive, 'no-cache') !== false) {
            header('Cache-Control: no-cache, must-revalidate'); // HTTP/1.1 
            header('Pragma: no-cache'); // HTTP/1.0 
        } else {
            header("Cache-Control: {$directive}, max-age={$max_age}");
            header("Pragma: {$directive}"); // HTTP/1.0
            // This Pragma header has an invalid value but we need to
            // overwrite any "Pragma: no-cache" possibly sent earlier
        }

        return true;
    }

    /**
     * Test to see if valid cache exists for this template
     *
     * Same as base Smarty function but honours the $this->force_regenerate
     * and $this->cache_id member variables.
     *
     * If the template is not cached or the cache has expired then the method
     * will return false, if a valid cache exists it will return true.
     *
     * If $this->force_regenerate is true then the method will always return
     * false regardless of whether any cache is available or not.
     *
     * @access  public
     * @param   boolean $must_validate  client must always validate requests
     * @param   string  $tpl_file       name of template file
     * @param   string  $cache_id       name of cache_id (optional)
     * @param   string  $compile_id     name of compile_id (optional)
     * @return  boolean                 true if cache exists
     */
    function is_cached($tpl_file, $cache_id = null, $compile_id = null) {
        if (!$this->caching || $this->force_regenerate)
            return false;

        if (!isset($cache_id))
            $cache_id = $this->cache_id;

        if (!isset($compile_id))
            $compile_id = $this->compile_id;

        return parent::is_cached($tpl_file, $cache_id, $compile_id);
    }

    /**
     * returns info about any cache for this template
     *
     * Similar to is_cached() but returns a hash of information about
     * the cache if it exists.
     *
     * If the template is not cached or the cache has expired then the method
     * will return null, if a valid cache exists it will return a hash array
     * containing the following elements;
     *   + 'created'        - time the cache was created
     *   + 'expires'        - time the cache expires
     *   + 'output'         - the actual cached output
     *
     * The expires time is dependent on the current setting of the
     * $caching and $cache_lifetime member variables.
     *
     * If $this->force_regenerate is true then the method will always return
     * null regardless of whether any cache is available or not.
     *
     * @access  public
     * @param   boolean $must_validate  client must always validate requests
     * @param   string  $tpl_file       name of template file
     * @param   string  $cache_id       name of cache_id (optional)
     * @param   string  $compile_id     name of compile_id (optional)
     * @return  array                   hash of cache information
     */
    function cache_info($tpl_file, $cache_id = null, $compile_id = null) {
        if (!$this->caching || $this->force_regenerate)
            return null;

        if (!isset($cache_id))
            $cache_id = $this->cache_id;

        if (!isset($compile_id))
            $compile_id = $this->compile_id;

        $_cache_info = $this->_cache_info;
        $this->_cache_info = array();

        $_params = array(
            'tpl_file' => $tpl_file,
            'cache_id' => $cache_id,
            'compile_id' => $compile_id,
            'results' => null
        );

        $ret = null;

        require_once(SMARTY_CORE_DIR . 'core.read_cache_file.php');
        if (smarty_core_read_cache_file($_params, $this)) {
            $ret = array(
                'created' => $this->_cache_info['timestamp'],
                'expires' => ($this->caching == 2 && isset ($this->_cache_info['expires']) ? $this->_cache_info['expires'] : ($this->cache_lifetime > -1 ? $this->_cache_info['timestamp'] + $this->cache_lifetime : -1)),
                'results' => $_params['results']
            );
        }

        $this->_cache_info = $_cache_info;
        return $ret;
    }

    /**
     * clear cached content for the given template and cache id
     *
     * Same as base Smarty function but honours the $this->cache_id
     * member variable.
     *
     * @access  public
     * @param   string  $tpl_file       name of template file
     * @param   string  $cache_id       name of cache_id (optional)
     * @param   string  $compile_id     name of compile_id (optional)
     * @param   string  $exp_time       expiration time (optional)
     * @return  boolean
     */
    function clear_cache($tpl_file = null, $cache_id = null, $compile_id = null, $exp_time = null) {
        if (!isset($cache_id))
            $cache_id = $this->cache_id;

        return parent::clear_cache($tpl_file, $cache_id, $compile_id, $exp_time);
    }

    /**
     * Compiles, executes & returns or displays a template.
     *
     * This is a replacement for the standard Smarty fetch() method. This
     * method has support for nested fetchs and sub caching. It also fixes
     * problems in the original smarty function where template debug
     * 'exec_time' were not filled in if the $display value was true.
     *
     * After fetching the output the modify_time() method will return the
     * time the returned output was last modified. If any non cached output
     * is included then this will be the current time, otherwise it will be
     * the time any of the output was last generated. Also the expiry_time()
     * method will return the time the output is currently set to expire, if
     * the output never expires then this will be -1, if caching is not
     * enabled then this will be 0.
     *
     * =======================================================================
     * Note: This method is where to look if the code fails - it replaces
     * a crucial Smarty method completely and thus if a new release of
     * Smarty has alterations to its fetch() method then this method must
     * be updated to reflect those.
     * =======================================================================
     *
     * @access  private
     * @param   string  $resource_name  template to display
     * @param   string  $cache_id       cache identifier
     * @param   string  $compile_id     compiler identifier
     * @param   boolean $display        display the template
     * @return  string                  template output
     */
    function fetch($resource_name, $cache_id = null, $compile_id = null, $display = false) {

        static $_cache_info = array();

        $_smarty_old_error_level = $this->debugging ? error_reporting() : error_reporting(isset($this->error_reporting)
            ? $this->error_reporting : error_reporting() & ~E_NOTICE);

        if (!$this->debugging && $this->debugging_ctrl == 'URL') {
            $_query_string = $this->request_use_auto_globals ? $_SERVER['QUERY_STRING'] : $GLOBALS['HTTP_SERVER_VARS']['QUERY_STRING'];
            if (@strstr($_query_string, $this->_smarty_debug_id)) {
                if (@strstr($_query_string, $this->_smarty_debug_id . '=on')) {
                    // enable debugging for this browser session
                    @setcookie('SMARTY_DEBUG', true);
                    $this->debugging = true;
                } elseif (@strstr($_query_string, $this->_smarty_debug_id . '=off')) {
                    // disable debugging for this browser session
                    @setcookie('SMARTY_DEBUG', false);
                    $this->debugging = false;
                } else {
                    // enable debugging for this page
                    $this->debugging = true;
                }
            } else {
                $this->debugging = (bool)($this->request_use_auto_globals ? @$_COOKIE['SMARTY_DEBUG'] : @$GLOBALS['HTTP_COOKIE_VARS']['SMARTY_DEBUG']);
            }
        }

        $this->_inclusion_depth++;

        if ($this->debugging) {
            // capture time for debugging info
            $_params = array();
            require_once(SMARTY_CORE_DIR . 'core.get_microtime.php');
            $_debug_start_time = smarty_core_get_microtime($_params, $this);
            $this->_smarty_debug_info[] = array('type'      => 'template',
                                                'filename'  => $resource_name,
                                                'depth'     => $this->_inclusion_depth);
            $_included_tpls_idx = count($this->_smarty_debug_info) - 1;
        }

        if (!isset($cache_id)) {
            $cache_id = $this->cache_id;
        }

        if (!isset($compile_id)) {
            $compile_id = $this->compile_id;
        }

        $this->_compile_id = $compile_id;
        $this->_cache_id = $cache_id;
        $this->_modify_time = null;
        $this->_expiry_time = null;

        // save old cache_info, initialize cache_info
        array_push($_cache_info, $this->_cache_info);

        $this->_cache_info = array();

        if ($this->caching && !$this->force_regenerate) {
           
            $_params = array(
                'tpl_file' => $resource_name,
                'cache_id' => $cache_id,
                'compile_id' => $compile_id,
                'results' => null
            );
            require_once(SMARTY_CORE_DIR . 'core.read_cache_file.php');
            if (smarty_core_read_cache_file($_params, $this)) {
                $this->_modify_time = $this->_cache_info['timestamp'];
                $this->_expiry_time = ($this->caching == 2 && isset ($this->_cache_info['expires']) ? $this->_cache_info['expires'] : ($this->cache_lifetime > -1 ? $this->_cache_info['timestamp'] + $this->cache_lifetime : -1));
                $_smarty_results = $_params['results'];
                if (!empty($this->_cache_info['insert_tags'])) {
                    $_params = array('plugins' => $this->_cache_info['insert_tags']);
                    require_once(SMARTY_CORE_DIR . 'core.load_plugins.php');
                    smarty_core_load_plugins($_params, $this);
                    // Wrap the insert plugins so we can hook into them
                    _mysmarty_wrap_insert_plugins($_params, $this);
                    $_params = array('results' => $_smarty_results);
                    require_once(SMARTY_CORE_DIR . 'core.process_cached_inserts.php');
                    $_smarty_results = smarty_core_process_cached_inserts($_params, $this);
                }
                if (!empty($this->_cache_info['cache_serials'])) {
                    $_params = array('results' => $_smarty_results);
                    require_once(SMARTY_CORE_DIR . 'core.process_compiled_include.php');
                    $_smarty_results = smarty_core_process_compiled_include($_params, $this);
                }

                if ($this->debugging) {
                    // capture time for debugging info
                    $_params = array();
                    require_once(SMARTY_CORE_DIR . 'core.get_microtime.php');
                    $this->_smarty_debug_info[$_included_tpls_idx]['exec_time'] = smarty_core_get_microtime($_params, $this) - $_debug_start_time;
                }

                if ($display) {
                    if ($this->cache_modified_check) {
                        $this->http_headers();
                        if (!$this->if_modified_since()) {
                            $output = null;
                        } else {
                            $output = $_smarty_results;
                        }
                    } else {
                        $output = $_smarty_results;
                    }

                    if (!is_null($output)) {
                        if ($this->debugging) {
                            require_once(SMARTY_CORE_DIR . 'core.display_debug_console.php');
                            $output .= smarty_core_display_debug_console($_params, $this);
                        }
                        echo $output;
                    }

                    $ret = true;
                } else {
                    $ret = $_smarty_results;
                }

                $this->_inclusion_depth--;

                // Restore error level
                error_reporting($_smarty_old_error_level);

                // restore initial cache_info
                $this->_cache_info = array_pop($_cache_info);

                return $ret;
            }
        }

        if ($this->caching) {
            $this->_cache_info['template'][$resource_name] = true;
        }

        // load filters that are marked as autoload
        if (count($this->autoload_filters)) {
            foreach ($this->autoload_filters as $_filter_type => $_filters) {
                foreach ($_filters as $_filter) {
                    $this->load_filter($_filter_type, $_filter);
                }
            }
        }

        $_smarty_compile_path = $this->_get_compile_path($resource_name);

        $this->_modify_time = time();
        $this->_expiry_time = ($this->caching ? ($this->cache_lifetime > -1 ? $this->_modify_time + $this->cache_lifetime : -1) : 0);

        $_cache_including = $this->_cache_including;
        $this->_cache_including = false;


        // if we just need to display the results, don't perform output
        // buffering - for speed
        if ($display && !$this->cache_modified_check && !$this->caching && count($this->_plugins['outputfilter']) == 0) {
            if ($this->_is_compiled($resource_name, $_smarty_compile_path)
                    || $this->_compile_resource($resource_name, $_smarty_compile_path)) {
                include($_smarty_compile_path);
            }
            $_smarty_results = '';
        } else {
            ob_start();
            if ($this->_is_compiled($resource_name, $_smarty_compile_path)
                    || $this->_compile_resource($resource_name, $_smarty_compile_path)) {
                include($_smarty_compile_path);
            }
            $_smarty_results = ob_get_contents();
            ob_end_clean();

            foreach ((array)$this->_plugins['outputfilter'] as $_output_filter) {
                $_smarty_results = call_user_func_array($_output_filter[0], array($_smarty_results, &$this));
            }
        }

        if ($this->caching) {
            $_params = array('tpl_file' => $resource_name,
                        'cache_id' => $cache_id,
                        'compile_id' => $compile_id,
                        'results' => $_smarty_results);
            require_once(SMARTY_CORE_DIR . 'core.write_cache_file.php');
            smarty_core_write_cache_file($_params, $this);
            require_once(SMARTY_CORE_DIR . 'core.process_cached_inserts.php');
            $_smarty_results = smarty_core_process_cached_inserts($_params, $this);

            if ($this->_cache_serials) {
                // strip nocache-tags from output
                $_smarty_results = preg_replace('!(\{/?nocache\:[0-9a-f]{32}#\d+\})!s'
                                                ,''
                                                ,$_smarty_results);
            }
        }
        $this->_cache_including = $_cache_including;

        if ($this->debugging) {
            // capture time for debugging info
            $_params = array();
            require_once(SMARTY_CORE_DIR . 'core.get_microtime.php');
            $this->_smarty_debug_info[$_included_tpls_idx]['exec_time'] = smarty_core_get_microtime($_params, $this) - $_debug_start_time;
        }

        if ($display) {
            if ($this->cache_modified_check) {
                $this->http_headers();
                if (!$this->if_modified_since()) {
                    $output = null;
                } else {
                    $output = $_smarty_results;
                }
            } else {
                $output = $_smarty_results;
            }

            if (!is_null($output)) {
                echo $output;
                if ($this->debugging) {
                    require_once(SMARTY_CORE_DIR . 'core.display_debug_console.php');
                    echo smarty_core_display_debug_console($_params, $this);
                }
            }

            $ret = true;
        } else {
            $ret = $_smarty_results;
        }

        $this->_inclusion_depth--;

        // restore initial cache_info
        $this->_cache_info = array_pop($_cache_info);

        // Restore error level
        error_reporting($_smarty_old_error_level);

        return $ret;
    }

    /**
     * Constructor
     *
     * @access  public
     */
    function MySmarty() {

        $this->cache_id = null;

        /* Ensure we use our overridden compiler class */
        $this->compiler_file =  MYSMARTY_DIR.'mysmarty_compiler.class.inc';
        $this->compiler_class = 'MySmarty_Compiler';

        // Register our custom tags
        $this->register_prefilter('_mysmarty_prefilter_cache');
        $this->register_compiler_function(MYSMARTY_TAG_FETCH, '_mysmarty_compiler_fetch', false, 1);
        $this->register_function(MYSMARTY_TAG_NOCACHE_VAR, '_mysmarty_function_nocache_var', false, 0);
        $this->register_block(MYSMARTY_TAG_NOCACHE, '_mysmarty_block_nocache', false, 1);

        // See if client requested non-cached page
        $this->force_regenerate = false;
        if (strcasecmp(@$_SERVER['REQUEST_METHOD'], 'GET') == 0 &&
            strcasecmp(@$_SERVER['HTTP_CACHE_CONTROL'], 'no-cache') == 0) {
            $this->force_regenerate = true;
        }
        
        // This are set by a valid call to fetch
        $this->_modify_time = null;
        $this->_expiry_time = null;

    }

    /**
     * Destructor
     *
     * @access  private
     */
    function _MySmarty() {

    }
}

// wrap the insert plugins so that they can check the modification time
function _mysmarty_wrapped_insert_plugin($name, $args, &$smarty) {

    $smarty->_has_set_modify_time = false;
    $smarty->_has_set_expiry_time = false;

    // call the original insert function
    $func = $smarty->_plugins['insert'][$name]['func'];
    $ret = $func($args, $smarty);

    if (!$smarty->_has_set_modify_time) {
        $smarty->modify_time(time());
    }
    if (!$smarty->_has_set_expiry_time) {
        $smarty->expiry_time(0);
    }

    return $ret;
}

// wrap the insert plugins so that they can check the modification time
function _mysmarty_wrap_insert_plugins($params, &$smarty) {

    foreach ($params['plugins'] as $_plugin_info) {
        list($_type, $_name, $_tpl_file, $_tpl_line, $_delayed_loading) = $_plugin_info;

        $_plugin = &$smarty->_plugins[$_type][$_name];

        $_plugin['func'] = $_plugin[0];
        $_plugin[0] = create_function('$args, &$smarty', "{ return _mysmarty_wrapped_insert_plugin('{$_name}', \$args, \$smarty); }");
    }
}

function _mysmarty_compiler_fetch($tag_args, &$smarty) {

    $attrs = $smarty->_parse_attrs($tag_args);
    $arg_list = array();

    if (empty($attrs['file']) && empty($attrs['_file'])) {
        $smarty->_syntax_error("missing 'file' attribute in '".MYSMARTY_TAG_FETCH."' tag", E_USER_ERROR, __FILE__, __LINE__);
    }

    $fetch_file = $plugin_name = $assign_var = null;

    foreach ($attrs as $arg_name => $arg_value) {
        if ($arg_name == '_file') {
            $fetch_file = $arg_value;
            continue;
        } else if (!$fetch_file && $arg_name == 'file') {
            $fetch_file = $arg_value;
            continue;
        } else if ($arg_name == '_name') {
            $plugin_name = $arg_value;
            continue;
        } else if (!$plugin_name && $arg_name == 'name') {
            $plugin_name = $arg_value;
            continue;
        } else if ($arg_name == '_assign') {
            $assign_var = $arg_value;
            continue;
        } else if (!$assign_var && $arg_name == 'assign') {
            $assign_var = $arg_value;
            continue;
        }
        if (is_bool($arg_value))
            $arg_value = $arg_value ? 'true' : 'false';
        if (is_null($arg_value))
            $arg_value = 'null';
        $arg_list[] = "'$arg_name' => $arg_value";
    }

    if ($plugin_name) {

        $found = false;
        $have_function = true;

        $name = $smarty->_dequote($plugin_name);

        /*
         * First we check if the 'fetch' function has already been registered
         * or loaded from a plugin file.
         */
        if (isset($smarty->_plugins['fetch'][$name])) {
            $plugin_func = $smarty->_plugins['fetch'][$name][0];
            if (!is_callable($plugin_func)) {
                $message = "fetch plugin function '$name' is not implemented";
            } else {
                $found = true;
            }
        }
        /*
         * Otherwise we need to load plugin file and look for the function
         * inside it.
         */
        else if ($plugin_file = $smarty->_get_plugin_filepath('fetch', $name)) {

            include_once $plugin_file;

            $plugin_func = 'smarty_fetch_' . $name;
            if (!function_exists($plugin_func)) {
                $message = "fetch plugin function $plugin_func() not found in $plugin_file\n";
            } else {
                $smarty->_plugins['fetch'][$name] = array($plugin_func, null, null, null, true);
                $found = true;

            }
        } else {
            $message = "could not load fetch plugin: $name";
        }

        if (!$found) {
            $smarty->_syntax_error($message, E_USER_WARNING, __FILE__, __LINE__);
            return null;
        }

        $smarty->_add_plugin('fetch', $name);
    }

    // Due to call order in _compile_compiler_tag() this won't be intialised if this is
    // the first non cached block encountered
    if (!isset($smarty->_cache_serial)) $smarty->_cache_serial = md5(uniqid('Smarty'));

    // All attrs are cached
    $_count = $smarty->_cache_attrs_count++;
    $output = "\$_cache_attrs =& \$this->_smarty_cache_attrs('$smarty->_cache_serial','$_count'); ";
    $output .= "if (!\$this->_cache_including) { \$_cache_attrs = array('file' => {$fetch_file}, 'name' => ".($plugin_name ? $plugin_name : 'null').", 'args' => ".($arg_list ? "array(".implode(',', $arg_list).")" : 'null')."); }   ";

    if ($assign_var) {
        $output .= "\$this->assign({$assign_var}, \$this->_mysmarty_fetch(\$_cache_attrs)); "; 
    } else {
        $output .= "echo \$this->_mysmarty_fetch(\$_cache_attrs); ";
    }

    return $output;
}

// The given variable should not be cached
function _mysmarty_function_nocache_var($param, &$smarty) {

    $content = @$param['value'];

    // Assume the output is now modified by this block
    // and that it expires immediately
    $smarty->modify_time(time());
    $smarty->expiry_time(0);

    EBET_Profile('None cached variable inserted with value: ', $content);

    return $content; 
}

// The given block should not be cached
function _mysmarty_block_nocache($param, $content, &$smarty, &$repeat) {

    if (is_null($content)) {
        /* On the open tag - setup the template variables */
        $smarty->assign($param);
        return null;
    }

    // Assume the output is now modified by this block
    // and that it expires immediately
    $smarty->modify_time(time());
    $smarty->expiry_time(0);

    EBET_Profile('None cached block of data inserted with content: ', $content);

    return $content; 
}


/* vim: set expandtab tabstop=4 shiftwidth=4: */

?>
