#!/usr/bin/env python
# -*- coding: UTF8 -*-

import fdb
import os
import time
import datetime
import cgi
import cgitb
cgitb.enable(context=1, format='text')
#import base64
#import struct
#import md5
#import json

# konfiguriramo vrijeme za prikaz u UTC
os.environ["TZ"] = "UTC"
time.tzset()

# uzmemo informaciju o posiljatelju
form = cgi.FieldStorage()

from hisveza import *

print "Content-type: text/html\r\n\r\n"

fbcon = None

# povezemo se na bazu
try:
    fbcon = fdb.connect(dsn=DB_HOST + ':' + DB_BASE, user=DB_USER, password=DB_PASS, charset='UTF8')
    #print "Povezano na bazu"
except fdb.Error:
    fbcon = None
    #print "Greska pri povezivanju na bazu: " + str(e)


# uzmemo trenutno vrijeme u CET zoni
#=============
def vrijeme():
    # struktura vremena (GMT)
    vrm = time.gmtime()
    # unix vrijeme (CEST)
    lvrm = int(time.mktime(vrm)) + 3600
    # date time struktura (y,m,d,h,mn,s) (CEST)
    vrmd = datetime.datetime.fromtimestamp(lvrm)
    # struktura vremena (CEST)
    vrm = time.gmtime(lvrm)
    # gsm string vremena
    vrms = time.strftime('"%y/%m/%d, %H:%M:%S"', vrm)
    return (lvrm, vrms, vrmd)


#==================
def izvrsisql(sql):
    if fbcon:
        try:
            cur = fbcon.cursor()
            cur.execute(sql)
            return cur
        except:
            return None
    else:
        return None


#====================
def poljatabele(tbl):
    try:
        cur = izvrsisql('SELECT * FROM ' + tbl)
        res = ''
        if cur:
            desc = cur.description
            res = ''
            for polje in desc:
                res = res + "%s;" % (polje[0])
            res = res + '\r\n'
            cur.close()
    except fdb.Error, e:
        res = "Error %d: %s" % (e.args[0], e.args[1])
    return res


#===============
def tabela(tbl):
    try:
        cur = izvrsisql('SELECT * FROM ' + tbl)
        res = ''
        if cur:
            rows = cur.fetchall()
            res = ''
            for red in rows:
                for polje in red:
                    res = res + "%s;" % (polje)
                res = res + '\r\n'
            cur.close()
    except fdb.Error, e:
        res = "Error %d: %s" % (e.args[0], e.args[1])
    return res


#=====================
def saljiVrijeme(tip):
    vmr = vrijeme()
    res = ''
    if tip == '2':
        res = vmr[1]
    else:
        res = str(vmr[0])
    return '[>vrm]' + res + '[<]\r\n'


#===============
def divGreska():
    print '<div style="font-family:Verdana, Arial, Helvetica, sans-serif; text-align: center; font-size: 1em" >'
    print '<hr />'
    print 'Greška'
    print '<hr />'
    print '</div>'


#==========================================
def TabelaBilten(cur, zag, align, stermin):
    if 'mini' in form:
        fonts = '70%'
        zagl = ['Vodotok', 'Postaja', 'Trend', 'Vodost (cm)', 'Dnevna tendenc.', 'Protok m<sup>3</sup>/s']
    else:
        fonts = '85%'
        zagl = ['Vodotok', 'Postaja', 'Trend', 'Vodostaj (cm)', 'Dnevna tendencija', 'Protok m<sup>3</sup>/s']

    shtml = '<div style="font-family:Verdana, Arial, Helvetica, sans-serif; border-collapse:collapse; font-size: ' + fonts + ';">' +\
        'Dnevni hidrološki izvještaj za ' + stermin + ' (UTC+1)<hr></div>'

    #table header
    shtml = shtml + '<table style="font-family:Verdana, Arial, Helvetica, sans-serif; border-collapse:collapse; font-size: ' + fonts + ';" ' +\
        'background="bg3.jpg" border="1" width="100%" cellpadding="2" bordercolor="888888">'

    if zag > 0:
        #desc = cur.description
        shtml = shtml + '<tr>'
        for polje in zagl:
            shtml = shtml + '<td align="center" background="bg4.jpg">'
            shtml = shtml + '<b>' + polje + '</b>'
            shtml = shtml + '</td>'
        shtml = shtml + '</tr>'

    #table body
    rows = cur.fetchall()

    nisti = 1
    svod = '??'
    tred = ''
    for red in rows:
        idx = 0
        for polje in red:
            if type(polje) is int:
                spolje = str(polje)
            else:
                polje = polje.encode('utf8')
                spolje = str(polje)

            if idx == 0:
                # prvo poje (vodotok)
                if spolje != svod:
                    # novi vodotok
                    if tred != '':
                        # zapisemo do sada formirame redove
                        if nisti > 1:
                            nisti = nisti + 1
                            tred = tred.replace('<th ', '<th rowspan="' + str(nisti) + '" ')
                        else:
                            tred = tred.replace('<tr>', '')
                            tred = tred.replace('</tr>', '')
                            tred = tred.replace('<th ', '<td ')
                            tred = tred.replace('</th>', '</td>')
                            tred = '<tr>' + tred + '</tr>'
                        shtml = shtml + tred
                        tred = ''
                        nisti = 1
                    tred = tred + '<tr><th align="' + align[idx] + '" style="padding-left:5px;padding-right:5px;">'
                    if str(polje) == 'None':
                        tred = tred + '&nbsp;'
                    else:
                        tred = tred + '<b>' + spolje + '</b>'
                    tred = tred + '</th></tr>'
                    svod = spolje
                else:
                    nisti = nisti + 1
                tred = tred + '<tr>'
            else:
                tred = tred + '<td align="' + align[idx] + '" style="padding-left:5px;padding-right:5px;">'
                if str(polje) == 'None':
                    tred = tred + '&nbsp;'
                else:
                    if idx == 2:
                        if spolje == '1':
                            spolje = '<img src="Up.gif" width="16" height="16">'
                        elif spolje == '-1':
                            spolje = '<img src="Down.gif" width="16" height="16">'
                        else:
                            spolje = '<img src="Stabile.gif" width="16" height="16">'
                    tred = tred + spolje
                tred = tred + '</td>'

            idx = idx + 1
        tred = tred + '</tr>'

    if nisti > 1:
        nisti = nisti + 1
        tred = tred.replace('<th ', '<th rowspan="' + str(nisti) + '" ')
    else:
        tred = tred.replace('<tr>', '')
        tred = tred.replace('</tr>', '')
        tred = tred.replace('<th ', '<td ')
        tred = tred.replace('</th>', '</td>')
        tred = '<tr>' + tred + '</tr>'
    shtml = shtml + tred
    return shtml


#================================
def Tabela2html(cur, zag, align):
    shtml = ''
    #table header
    shtml = shtml + '<table style="font-family:Verdana, Arial, Helvetica, sans-serif; border-collapse:collapse; font-size: 80%;" ' +\
        'background="bg3.jpg" border="1" width="100%" cellpadding="2" bordercolor="888888">'

    rcl = 0
    if zag > 0:
        desc = cur.description
        shtml = shtml + '<tr bgcolor=#e0eeee>'
        for polje in desc:
            shtml = shtml + '<td align="center" background="bg4.jpg">'
            shtml = shtml + '<b>' + polje[0] + '</b>'
            shtml = shtml + '</td>'
        shtml = shtml + '</tr>'
        rcl = 1

    #table body
    rows = cur.fetchall()

    for red in rows:
        shtml = shtml + '<tr>'

        idx = 0
        for polje in red:
            polje = polje.encode('utf8')
            if rcl == 1:
                shtml = shtml + '<td align="' + align[idx] + '" style="padding-left:5px;padding-right:5px;">'
            else:
                shtml = shtml + '<td align="' + align[idx] +\
                    '" style="padding-left:5px;padding-right:5px;" background="bg2.jpg">'

            if str(polje) == 'None':
                shtml = shtml + '&nbsp;'
            else:
                spolje = str(polje)
                if spolje.find('m3/s') != -1:
                    spolje = spolje.replace('m3/s', 'm<sup>3</sup>/s')
                shtml = shtml + spolje

            shtml = shtml + '</td>'
            idx = idx + 1

        shtml = shtml + '</tr>'
        rcl = rcl + 1
        if rcl > 1:
            rcl = 0

    shtml = shtml + '</table>'
    return shtml


#===================
def Zapis2html(cur):
    shtml = ''
    #table header
    shtml = shtml + '<table style="font-family:Verdana, Arial, Helvetica, sans-serif; border-collapse:collapse; font-size:' +\
        ' 80%;" bgcolor=#f0ffff background="bg3.jpg" border="1" width="100%" cellpadding="2" bordercolor="888888">'
    try:
        #table body
        rows = cur.fetchall()
        desc = cur.description
        if len(rows) > 0:
            rcl = 0
            idx = 0
            for polje in desc:
                shtml = shtml + '<tr>'

                if rcl == 1:
                    shtml = shtml + '<td align="right" style="padding-left:5px;padding-right:5px;">'
                else:
                    shtml = shtml + '<td align="right" style="padding-left:5px;padding-right:5px;" background="bg2.jpg">'
                if str(polje) == 'None':
                    shtml = shtml + '&nbsp;'
                else:
                    imep = polje[0]
                    if imep.find('Topografska') != -1:
                        imep = 'Topografska površina sliva (km<sup>2</sup>)'
                    shtml = shtml + imep
                shtml = shtml + '</td>'

                if rcl == 1:
                    shtml = shtml + '<td align="right" style="padding-left:5px;padding-right:5px;">'
                else:
                    shtml = shtml + '<td align="right" style="padding-left:5px;padding-right:5px;" background="bg2.jpg">'
                if rows[0][idx]:
                    if type(rows[0][idx]) is datetime.date:
                        shtml = shtml + "%02d. %02d. %04d." % (rows[0][idx].day, rows[0][idx].month, rows[0][idx].year)
                    elif type(rows[0][idx]) is float:
                        spod = "%.3f" % rows[0][idx]
                        spod = spod.replace('.', ',')
                        shtml = shtml + spod
                    elif type(rows[0][idx]) is int:
                        shtml = shtml + str(rows[0][idx])
                    else:
                        shtml = shtml + rows[0][idx].encode('utf8')
                else:
                    shtml = shtml + '&nbsp;--'

                shtml = shtml + '</td>'

                shtml = shtml + '</tr>'
                rcl = rcl + 1
                if rcl > 1:
                    rcl = 0
                idx = idx + 1
    except:
        pass

    shtml = shtml + '</table>'
    return shtml


#====================
def mjerenjaPostaje():
    post = ''
    cur = izvrsisql("select IME, IME_VODOTOKA from POSTAJE_SVE where KOD = " + form.getvalue('kpost'))
    try:
        if cur:
            rows = cur.fetchall()
            if len(rows) > 0:
                post = rows[0][0].encode('utf8') + ', ' + rows[0][1].encode('utf8')
    except:
        post = '??'

    try:
        print '<div style="font-family:Verdana, Arial, Helvetica, sans-serif; text-align: center; font-size: 1em" >'
        print '<hr />'
        print 'Mjerenja postaje: <b><font charset=UTF-8; color="#0B0B61";> ' + post + '</font></b>'
        print '<hr />'
        print '</div>'

        cur = izvrsisql('select VRSTA_MJERENJA as "Vrsta mjerenja",'
                        ' (select * FROM PROC_GODINE_MJERENJA(KOD_POSTAJE,TIP_MJERENJA)) as "Godine/broj mjerenja"'
                        ' from MJERENJA where KOD_POSTAJE = ' + form.getvalue('kpost') + " order by RBR_TIPA")
        if cur:
            print Tabela2html(cur, 1, ['right', 'left'])
            cur.close()
        else:
            print 'g1'
    except:
        divGreska()


#=================
def infoPostaje():
    try:
        print '<div style="font-family:Verdana, Arial, Helvetica, sans-serif; text-align: center; font-size: 1em" >'
        print '<hr />'
        print 'Osnovni podaci postaje'
        print '<hr />'
        print '</div>'

        if form.getvalue('kpost') > 0:
            cur = izvrsisql('select IME as "Ime", SIFRA as "Šifra", IME_VODOTOKA as "Vodotok", '
                            'IME_SLIVA as "Slivno područje", POCETAK_RADA as "Početak rada", KRAJ_RADA as "Kraj rada",'
                            ' KOTA_NULE as "Kota nule vodokaza (m n/m)" from POSTAJE_SVE where KOD = ' +
                            form.getvalue('kpost'))
            if cur:
                print Zapis2html(cur)
                cur.close()
            else:
                divGreska()
    except:
        divGreska()


#=========================
def infoMjerenjaPostaje():
    try:
        print '<div style="font-family:Verdana, Arial, Helvetica, sans-serif; text-align: center; font-size: 90%" >'
        print '<hr />'
        print 'Mjerenja postaje'
        print '<hr />'
        print '</div>'

        cur = izvrsisql('select MJERENJE as "Vrsta mjerenja", INFO as "Info" from TINFO_MJERENJA_POSTAJE(' +
                        form.getvalue('kpost') + ')')
        if cur:
            print Tabela2html(cur, 1, ['right', 'left'])
            cur.close()
        else:
            divGreska()

    except:
        divGreska()


#============
def Bilten():
    try:
        tip = 1
        if 'btip' in form:
            tip = int(form.getvalue('btip'))
    except:
        tip = 1

    tip = 1
    try:
        if 'zadpod' in form:
            term = datetime.datetime.utcnow() + datetime.timedelta(0, 3600)
            if term.hour < 7:
                term = term - datetime.timedelta(days=1)
            strm = str(term.year) + '-' + str(term.month) + '-' + str(term.day) + ' ' + str(term.hour) + ':00:00'
            sstrm = str(term.day) + '. ' + str(term.month) + '. ' + str(term.year) + '. ' + str(term.hour)  + ':00:00'
        else:
            term = datetime.datetime.utcnow() + datetime.timedelta(0, 3600)
            if term.hour < 8:
                term = term - datetime.timedelta(days=1)
            strm = str(term.year) + '-' + str(term.month) + '-' + str(term.day) + ' 07:00:00'
            sstrm = str(term.day) + '. ' + str(term.month) + '. ' + str(term.year) + '. 07:00:00'

        cur = izvrsisql("select VODOTOK,POSTAJA,TREND,VODOSTAJ,TENDENCIJA,PROTOK from WEB_BILTEN('" + strm + "', " + str(tip) + ")")
        if cur:
            print TabelaBilten(cur, 1, ['center', 'left', 'center', 'center', 'center', 'center'], sstrm)
            cur.close()
        else:
            print 'cur greska'
            #divGreska()

    except Exception, e:
        print 'GRESKA ' + str(e)
        #divGreska()


#=============
def BiltenX():
    try:
        tip = 1
        if 'btip' in form:
            tip = int(form.getvalue('btip'))
    except:
        tip = 1

    try:
        if 'zadpod' in form:
            term = datetime.datetime.utcnow() + datetime.timedelta(0, 3600)
            if term.hour < 7:
                term = term - datetime.timedelta(days=1)
            strm = str(term.year) + '-' + str(term.month) + '-' + str(term.day) + ' ' + str(term.hour) + ':00:00'
            sstrm = str(term.day) + '. ' + str(term.month) + '. ' + str(term.year) + '. ' + str(term.hour)  + ':00:00'
        else:
            term = datetime.datetime.utcnow() + datetime.timedelta(0, 3600)
            if term.hour < 8:
                term = term - datetime.timedelta(days=1)
            strm = str(term.year) + '-' + str(term.month) + '-' + str(term.day) + ' 07:00:00'
            sstrm = str(term.day) + '. ' + str(term.month) + '. ' + str(term.year) + '. 07:00:00'

        cur = izvrsisql("select VODOTOK,POSTAJA,TREND,VODOSTAJ,TENDENCIJA,PROTOK from WEB_BILTEN_X('" + strm + "', " + str(tip) + ")")
        if cur:
            print TabelaBilten(cur, 1, ['center', 'left', 'center', 'center', 'center', 'center'], sstrm)
            cur.close()
        else:
            print 'cur greska'
            #divGreska()

    except Exception, e:
        print 'GRESKA ' + str(e)
        #divGreska()


#=================
def punInfoPostaje():
    sirprof = 40
    if ('sirina' in form) and ('visina' in form):
        try:
            sirprof = int(form.getvalue('sirina'))
            visprof = int(form.getvalue('visina')) + 220
            sirprof = (sirprof * 100) / visprof
        except:
            sirprof = 40

    post = '??'
    koment = 'None'
    cur = izvrsisql("select SIFRA, KOMENTAR from POSTAJE where KOD = " + form.getvalue('kpost'))
    try:
        if cur:
            rows = cur.fetchall()
            if len(rows) > 0:
                post = str(rows[0][0])
                try:
                    koment = rows[0][1].encode('utf8')
                except:
                    koment = 'None'
    except:
        post = '??'

    try:
        # slika
        if post != '??':
            lpath = os.getcwd()
            sifpost = post

            lpost = lpath[0:lpath.rfind('/')] + '/slike/' + sifpost + '.jpg'
            post = '../slike/' + sifpost + '.jpg'
            if os.path.isfile(lpost):
                print '<div style="font-family:Verdana, Arial, Helvetica, sans-serif; text-align: center; font-size: 80%" >'
                print '<hr />'
                print '<img src="' + post + '" alt="Postaja" width="100%">'
                if koment != 'None':
                    print '&bull;&bull;&bull;'
                print '</div>'

            if koment != 'None':
                print '<div style="font-family:Verdana, Arial, Helvetica, sans-serif; text-align: justify; font-size: 80%" >'
                #print '<br>'
                print koment
                print '</div>'

            print '<div style="font-family:Verdana, Arial, Helvetica, sans-serif; text-align: center; font-size: 90%" >'
            print '<hr />'
            print 'Osnovni podaci postaje'
            print '<hr />'
            print '</div>'

            if form.getvalue('kpost') > 0:
                cur = izvrsisql('select * FROM INFO_POSTAJE(' + form.getvalue('kpost') + ')')
                if cur:
                    print Zapis2html(cur)
                    cur.close()
                else:
                    divGreska()

            infoMjerenjaPostaje()

            lpost = lpath[0:lpath.rfind('/')] + '/slike/' + sifpost + '.svg'
            post = '../slike/' + sifpost + '.svg'
            if os.path.isfile(lpost):
                #print '<div>'
                print '<hr />'
                print '<object type="image/svg+xml" width="100%" height="' + str(sirprof) + '%" data="' +\
                      post + '">SVG nije podržan!</object>'
                #print '<iframe src="' + post + '">SVG nije podržan</iframe>'
                #print '</div>'

        print '<div style="font-family:Verdana, Arial, Helvetica, sans-serif; text-align: center; font-size: 1em" >'
        print '<hr />'
        print '</div>'
    except:
        divGreska()


#===================
def profilPostaje():
    post = '??'
    cur = izvrsisql("select SIFRA from POSTAJE where KOD = " + form.getvalue('kpost'))
    try:
        if cur:
            rows = cur.fetchall()
            if len(rows) > 0:
                post = str(rows[0][0])
    except:
        post = '??'

    try:
        # slika
        if post != '??':
            lpath = os.getcwd()
            sifpost = post
            lpost = lpath[0:lpath.rfind('/')] + '/slike/' + sifpost + '.svg'
            post = '../slike/' + sifpost + '.svg'
            if os.path.isfile(lpost):
                #print '<div>'
                if 'fullsize' in form:
                    print '<object type="image/svg+xml" data="' + post + '">SVG nije podržan!</object>'
                else:
                    print '<object type="image/svg+xml" width="100%" height="96%" data="' + post +\
                          '">SVG nije podržan!</object>'
                #print '<iframe src="' + post + '">SVG nije podržan</iframe>'
                #print '</div>'
            else:
                print '<div>'
                print 'Nema profila postaje.'
                print '</div>'
        else:
            divGreska()
    except:
        divGreska()


#=================
def slivVodInfo():
    try:
        print '<div style="background-image: url(bg2.jpg); font-family:Arial, Helvetica, sans-serif; ' +\
            'text-align: center; font-size: 1em; width:450px;float:left;" >'
        cur = izvrsisql('select IME as "Slivno područje", BROJ_VODOTOKOVA as "Broj vodotokova", '
                        'BROJ_POSTAJA as "Broj postaja" from SLIVNA_PODRUCJA where KOD=' + form.getvalue('ksliv'))
        if cur:
            print Zapis2html(cur)
            cur.close()
        else:
            pass
        print '</div>'
        print '<div style="background-image: url(bg2.jpg); font-family:Arial, Helvetica, sans-serif; ' +\
            'text-align: center; font-size: 1em; width:450px; float:right;" >'
        cur = izvrsisql('select IME as "Vodotok", BROJ_POSTAJA as "Broj postaja" from VODOTOCI where KOD=' +
                        form.getvalue('kvod'))
        if cur:
            print Zapis2html(cur)
            cur.close()
        else:
            pass
        print '</div>'
    except:
        pass


#==============
def pozadina():
    try:
        print '<div style="background-image: url(bg2.jpg);" >'
        print '</div>'

    except:
        divGreska()


#==========================================================================================

print '<head><meta http-equiv="Content-Type" content= "text/html; charset=utf-8"></head><body background="bg1.png">'

if 'funkc' in form:
    #---------------------------------------
    if form.getvalue('funkc') == 'mjerpost':
        if 'kpost' in form:
            mjerenjaPostaje()

    #---------------------------------------
    if form.getvalue('funkc') == 'infopost':
        if 'kpost' in form:
            infoPostaje()

    #------------------------------------------
    if form.getvalue('funkc') == 'puninfopost':
        if 'kpost' in form:
            punInfoPostaje()

    #------------------------------------------
    if form.getvalue('funkc') == 'bilten':
        Bilten()

    #------------------------------------------
    if form.getvalue('funkc') == 'xbilten':
        BiltenX()

    #------------------------------------------
    if form.getvalue('funkc') == 'profilpost':
        if 'kpost' in form:
            profilPostaje()

    #---------------------------------------
    if form.getvalue('funkc') == 'slivvod':
        if ('ksliv' in form) and ('kvod' in form):
            slivVodInfo()

    #---------------------------------------
    if form.getvalue('funkc') == 'pozadina':
        pozadina()

print '</body>'

if fbcon:
    fbcon.close()
