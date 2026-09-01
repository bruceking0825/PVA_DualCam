class Settings():
    # APP SETTINGS
    # ///////////////////////////////////////////////////////////////
    ENABLE_CUSTOM_TITLE_BAR = True
    MENU_WIDTH = 240
    LEFT_BOX_WIDTH = 240
    RIGHT_BOX_WIDTH = 240
    TIME_ANIMATION = 500

    # BTNS LEFT AND RIGHT BOX COLORS
    BTN_LEFT_BOX_COLOR = "background-color: rgb(44, 49, 58);"
    BTN_RIGHT_BOX_COLOR = "background-color: #ff79c6;"

    # MENU SELECTED STYLESHEET
    MENU_SELECTED_STYLESHEET = """
    border-left: 22px solid qlineargradient(spread:pad, x1:0.034, y1:0, x2:0.216, y2:0, stop:0.499 rgba(255, 121, 198, 255), stop:0.5 rgba(85, 170, 255, 0));
    background-color: rgb(86, 99, 136);
    """

    WIDGET_TAB_STYLE = """QTabBar::tab:selected { 
        border-left: 2px solid rgb(255, 121, 198); 
        background-color: rgb(86, 99, 136); 
        color: rgb(255, 255, 255); 
    }"""

    LABEL_SELECTED_STYLE = """QLabel { 
        border-left: 2px solid rgb(189, 147, 249); 
        background-color: rgb(86, 99, 136); 
        color: rgb(255, 255, 255); 
    }"""

    PARAMTER_FORM_STYLE = """QWidget#formContainer {
        border: 2px solid rgb(70, 80, 110);
        background: transparent;
        padding: 0px;
    }"""

    BUTTON_ONLINE = '''
        background-color: rgb(0, 170, 0);
        color: white;    
    '''
    BUTTON_OFFLINE = '''
        background-color: rgb(170, 0, 0);
        color: white;    
    '''
    BUTTON_ON = "background-color: rgb(0, 145, 55); color: white;"

    BUTTON_OFF = "background-color: rgb(70, 80, 110); color: white;"
    
    RES_OK = '''
        background-color: rgb(0, 170, 0);                
        font-family: 'Microsoft YaHei UI';
        font-size: 25pt;
        color: rgb(0, 0, 0);
        font-weight: bold;
    '''
    RES_NG = '''
        background-color: rgb(170, 0, 0);          
        font-family: 'Microsoft YaHei UI';
        font-size: 25pt;
        color: white;
        font-weight: bold;
    '''
    RES_ERROR = '''
        background-color: rgb(170, 170, 0);                
        font-family: 'Microsoft YaHei UI';
        font-size: 25pt;
        color: white;
        font-weight: bold;
    '''
    RES_NONE = '''
        background-color: rgb(170, 170, 170);                
        font-family: 'Microsoft YaHei UI';
        font-size: 25pt;
        color: white;
        font-weight: bold;
    '''
